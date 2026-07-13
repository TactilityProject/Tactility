// SPDX-License-Identifier: Apache-2.0
#include <drivers/es7210.h>

#include <tactility/device.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/drivers/audio_codec_adapters.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/i2s_controller.h>
#include <tactility/log.h>

#include <es7210_adc.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>

#include <cstring>
#include <vector>

#define TAG "ES7210"

namespace {

constexpr uint32_t NATIVE_SAMPLE_RATE = 16000;
constexpr float MAX_INPUT_GAIN_DB = 37.5f; // ES7210 mic gain range is roughly 0..37.5 dB

struct Es7210Data {
    const audio_codec_ctrl_if_t* ctrlIf = nullptr;
    const audio_codec_data_if_t* dataIf = nullptr;
    const audio_codec_if_t* codecIf = nullptr;
    esp_codec_dev_handle_t codecDevice = nullptr;
    bool isOpen = false;
    uint8_t micCount = 4;
    uint8_t micMask = 0x0F;
    uint8_t tdmSlotCount = 4;
    uint8_t openBitsPerSample = 16;
    uint32_t openSampleRate = 0;
    float inputGain = 1.0f;
    std::vector<uint8_t> rawBuffer;
};

#define GET_CONFIG(device) (static_cast<const Es7210Config*>((device)->config))
#define GET_DATA(device) (static_cast<Es7210Data*>(device_get_driver_data(device)))

// region AudioCodecApi

error_t open_(Device* device, const struct AudioCodecStreamConfig* config) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr) {
        return ERROR_RESOURCE;
    }

    if (config->direction == AUDIO_CODEC_DIR_OUTPUT || config->direction == AUDIO_CODEC_DIR_BOTH) {
        LOG_E(TAG, "ES7210 is input-only");
        return ERROR_NOT_SUPPORTED;
    }

    if (data->isOpen) {
        bool sameConfig = config->bits_per_sample == data->openBitsPerSample
            && config->sample_rate == data->openSampleRate;
        return sameConfig ? ERROR_NONE : ERROR_RESOURCE;
    }

    // Always configure the hardware with the full TDM slot count, never the (possibly
    // smaller) active mic count: es7210_set_fs() silently halves the configured bit depth
    // when asked for <= 2 channels in TDM mode (channel_mask == 0), corrupting audio.
    // We demux down to the active mic slots ourselves in read_().
    uint8_t hwChannels = data->tdmSlotCount;

    esp_codec_dev_sample_info_t sampleInfo = {
        .bits_per_sample = config->bits_per_sample,
        .channel = hwChannels,
        .channel_mask = 0,
        .sample_rate = config->sample_rate,
        .mclk_multiple = 0,
    };

    if (esp_codec_dev_open(data->codecDevice, &sampleInfo) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open codec device");
        return ERROR_RESOURCE;
    }

    data->openBitsPerSample = config->bits_per_sample;
    data->openSampleRate = config->sample_rate;
    data->isOpen = true;
    return ERROR_NONE;
}

error_t close_(Device* device) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr) {
        return ERROR_RESOURCE;
    }

    if (data->isOpen) {
        esp_codec_dev_close(data->codecDevice);
        data->isOpen = false;
    }

    return ERROR_NONE;
}

error_t read_(Device* device, void* buffer, size_t size, size_t* bytesRead, TickType_t timeout) {
    (void) timeout;
    auto* data = GET_DATA(device);
    if (data == nullptr || !data->isOpen) {
        return ERROR_RESOURCE;
    }

    // The hardware always runs at tdmSlotCount channels (see open_ for why); demux down
    // to just the active mic slots here so callers see exactly micCount channels of real
    // signal -- never diluted by silent/AEC-reference TDM slots.
    if (data->micCount == data->tdmSlotCount) {
        int result = esp_codec_dev_read(data->codecDevice, buffer, (int) size);
        if (result < 0) {
            return ERROR_RESOURCE;
        }
        if (bytesRead != nullptr) {
            *bytesRead = (size_t) result;
        }
        return ERROR_NONE;
    }

    uint8_t bytesPerSample = (uint8_t) (data->openBitsPerSample / 8);
    size_t outFrameBytes = (size_t) data->micCount * bytesPerSample;
    size_t requestedFrames = (outFrameBytes != 0) ? (size / outFrameBytes) : 0;
    if (requestedFrames == 0) {
        if (bytesRead != nullptr) {
            *bytesRead = 0;
        }
        return ERROR_NONE;
    }

    size_t rawFrameBytes = (size_t) data->tdmSlotCount * bytesPerSample;
    size_t rawBytesNeeded = requestedFrames * rawFrameBytes;
    if (data->rawBuffer.size() < rawBytesNeeded) {
        data->rawBuffer.resize(rawBytesNeeded);
    }

    int result = esp_codec_dev_read(data->codecDevice, data->rawBuffer.data(), (int) rawBytesNeeded);
    if (result < 0) {
        return ERROR_RESOURCE;
    }

    size_t rawBytesRead = (size_t) result;
    size_t framesRead = rawBytesRead / rawFrameBytes;

    // Demux: pick the bytesPerSample-wide slot for each set bit in micMask, in ascending
    // slot order, e.g. micMask = MIC1|MIC3 -> slots 0 and 2.
    uint8_t slotIndices[8];
    uint8_t slotCount = 0;
    for (uint8_t slot = 0; slot < data->tdmSlotCount && slotCount < sizeof(slotIndices); slot++) {
        if ((data->micMask & (uint8_t) (1u << slot)) != 0) {
            slotIndices[slotCount++] = slot;
        }
    }

    auto* out = static_cast<uint8_t*>(buffer);
    const uint8_t* raw = data->rawBuffer.data();
    for (size_t frame = 0; frame < framesRead; frame++) {
        const uint8_t* rawFrame = raw + frame * rawFrameBytes;
        uint8_t* outFrame = out + frame * outFrameBytes;
        for (uint8_t ch = 0; ch < slotCount && ch < data->micCount; ch++) {
            std::memcpy(outFrame + (size_t) ch * bytesPerSample, rawFrame + (size_t) slotIndices[ch] * bytesPerSample, bytesPerSample);
        }
    }

    if (bytesRead != nullptr) {
        *bytesRead = framesRead * outFrameBytes;
    }
    return ERROR_NONE;
}

error_t write_(Device* device, const void* buffer, size_t size, size_t* bytesWritten, TickType_t timeout) {
    (void) device;
    (void) buffer;
    (void) size;
    (void) bytesWritten;
    (void) timeout;
    return ERROR_NOT_SUPPORTED;
}

error_t setVolume(Device* device, enum AudioCodecDirection direction, float volumePercent) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr || direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    // Map 0..100% linearly onto the gain range.
    float db = (volumePercent / 100.0f) * MAX_INPUT_GAIN_DB;
    return (esp_codec_dev_set_in_gain(data->codecDevice, db) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t getVolume(Device* device, enum AudioCodecDirection direction, float* volumePercent) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr || direction != AUDIO_CODEC_DIR_INPUT || volumePercent == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    float db = 0.0f;
    if (esp_codec_dev_get_in_gain(data->codecDevice, &db) != ESP_CODEC_DEV_OK) {
        return ERROR_RESOURCE;
    }
    *volumePercent = (db / MAX_INPUT_GAIN_DB) * 100.0f;
    return ERROR_NONE;
}

error_t setMute(Device* device, enum AudioCodecDirection direction, bool muted) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr || direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    return (esp_codec_dev_set_in_mute(data->codecDevice, muted) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t getMute(Device* device, enum AudioCodecDirection direction, bool* muted) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr || direction != AUDIO_CODEC_DIR_INPUT || muted == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    return (esp_codec_dev_get_in_mute(data->codecDevice, muted) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t getNativeChannels(Device* device, enum AudioCodecDirection direction, uint8_t* channels) {
    auto* data = GET_DATA(device);
    if (data == nullptr || direction != AUDIO_CODEC_DIR_INPUT || channels == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *channels = data->micCount;
    return ERROR_NONE;
}

error_t getNativeSampleRate(Device* device, enum AudioCodecDirection direction, uint32_t* rateHz) {
    (void) device;
    if (direction != AUDIO_CODEC_DIR_INPUT || rateHz == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *rateHz = NATIVE_SAMPLE_RATE;
    return ERROR_NONE;
}

error_t getCapabilities(Device* device, enum AudioCodecDirection* supportedDirections) {
    (void) device;
    if (supportedDirections == nullptr) {
        return ERROR_RESOURCE;
    }
    *supportedDirections = AUDIO_CODEC_DIR_INPUT;
    return ERROR_NONE;
}

error_t getInputGainMultiplier(Device* device, float* gain) {
    auto* data = GET_DATA(device);
    if (data == nullptr || gain == nullptr) {
        return ERROR_RESOURCE;
    }
    *gain = data->inputGain;
    return ERROR_NONE;
}

const struct AudioCodecApi API = {
    .open = open_,
    .close = close_,
    .read = read_,
    .write = write_,
    .set_volume = setVolume,
    .get_volume = getVolume,
    .set_mute = setMute,
    .get_mute = getMute,
    .get_native_sample_rate = getNativeSampleRate,
    .get_native_channels = getNativeChannels,
    .get_capabilities = getCapabilities,
    .get_input_gain_multiplier = getInputGainMultiplier,
};

// endregion

// region Driver lifecycle

error_t startDevice(Device* device) {
    const auto* config = GET_CONFIG(device);

    // devicetree's int property type has no min/max constraint support, so a bogus
    // negative literal would otherwise silently wrap to 65535 when narrowed to uint16_t.
    // Bound it here instead -- 2000 (20x) is already an extreme upper bound for
    // compensating a quiet mic capsule on top of the ES7210's own hardware gain.
    if (config->input_gain_x100 > 2000) {
        LOG_E(TAG, "Invalid input_gain_x100 %u (must be 0..2000)", config->input_gain_x100);
        return ERROR_RESOURCE;
    }

    // Same rationale as input_gain_x100: devicetree ints aren't bit-constrained, so
    // reject any bit outside the four supported mic slots before it inflates micCount
    // beyond the fixed 4-slot TDM frame that read_() demuxes against.
    constexpr uint8_t supportedMicMask = (uint8_t) (ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4);
    if ((config->mic_selected_mask & ~supportedMicMask) != 0) {
        LOG_E(TAG, "Invalid mic_selected_mask 0x%02X (must be subset of 0x%02X)", config->mic_selected_mask, supportedMicMask);
        return ERROR_RESOURCE;
    }

    auto* i2cController = device_get_parent(device);
    if (i2cController == nullptr || device_get_type(i2cController) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto* i2sController = device_find_by_name(config->i2s_device_name);
    if (i2sController == nullptr || device_get_type(i2sController) != &I2S_CONTROLLER_TYPE) {
        LOG_E(TAG, "I2S controller '%s' not found", config->i2s_device_name);
        return ERROR_RESOURCE;
    }

    auto* data = new Es7210Data();

    uint8_t micMask = (config->mic_selected_mask != 0)
        ? config->mic_selected_mask
        : (uint8_t) (ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4);
    data->micMask = micMask;
    data->micCount = (uint8_t) __builtin_popcount(micMask);
    data->tdmSlotCount = 4;
    data->inputGain = (float) config->input_gain_x100 / 100.0f;

    data->ctrlIf = audio_codec_adapter_new_i2c_ctrl(i2cController, config->address);
    data->dataIf = audio_codec_adapter_new_i2s_data(i2sController);
    if (data->ctrlIf == nullptr || data->dataIf == nullptr) {
        LOG_E(TAG, "Failed to create adapters");
        goto cleanup;
    }

    if (data->ctrlIf->open(data->ctrlIf, nullptr, 0) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open control interface");
        goto cleanup;
    }

    if (data->dataIf->open(data->dataIf, nullptr, 0) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open data interface");
        goto cleanup;
    }

    {
        es7210_codec_cfg_t codecConfig = {};
        codecConfig.ctrl_if = data->ctrlIf;
        codecConfig.master_mode = false;
        codecConfig.mic_selected = micMask;
        codecConfig.mclk_src = ES7210_MCLK_FROM_PAD;
        codecConfig.mclk_div = 0;

        data->codecIf = es7210_codec_new(&codecConfig);
        if (data->codecIf == nullptr) {
            LOG_E(TAG, "Failed to create ES7210 codec interface");
            goto cleanup;
        }
    }

    {
        esp_codec_dev_cfg_t devConfig = {
            .dev_type = ESP_CODEC_DEV_TYPE_IN,
            .codec_if = data->codecIf,
            .data_if = data->dataIf,
        };

        data->codecDevice = esp_codec_dev_new(&devConfig);
        if (data->codecDevice == nullptr) {
            LOG_E(TAG, "Failed to create codec device");
            goto cleanup;
        }
    }

    device_set_driver_data(device, data);
    return ERROR_NONE;

cleanup:
    // Mirrors stopDevice's teardown order -- delete_*_if() routines close their interface
    // first, so we don't need separate ->close() calls here.
    if (data->codecDevice != nullptr) {
        esp_codec_dev_delete(data->codecDevice);
    }
    if (data->codecIf != nullptr) {
        audio_codec_delete_codec_if(data->codecIf);
    }
    if (data->dataIf != nullptr) {
        audio_codec_delete_data_if(data->dataIf);
    }
    if (data->ctrlIf != nullptr) {
        audio_codec_delete_ctrl_if(data->ctrlIf);
    }
    delete data;
    return ERROR_RESOURCE;
}

error_t stopDevice(Device* device) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_NONE;
    }

    if (data->isOpen) {
        esp_codec_dev_close(data->codecDevice);
    }

    if (data->codecDevice != nullptr) {
        esp_codec_dev_delete(data->codecDevice);
    }
    if (data->codecIf != nullptr) {
        audio_codec_delete_codec_if(data->codecIf);
    }
    if (data->dataIf != nullptr) {
        audio_codec_delete_data_if(data->dataIf);
    }
    if (data->ctrlIf != nullptr) {
        audio_codec_delete_ctrl_if(data->ctrlIf);
    }

    device_set_driver_data(device, nullptr);
    delete data;
    return ERROR_NONE;
}

// endregion

} // namespace

extern "C" {

Driver es7210_driver = {
    .name = "es7210",
    .compatible = (const char*[]) { "everest,es7210", nullptr },
    .start_device = startDevice,
    .stop_device = stopDevice,
    .api = &API,
    .device_type = &AUDIO_CODEC_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

}
