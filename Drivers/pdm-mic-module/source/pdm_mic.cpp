// SPDX-License-Identifier: Apache-2.0
#include <drivers/pdm_mic.h>

#include <tactility/device.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/drivers/audio_codec_adapters.h>
#include <tactility/drivers/i2s_controller.h>
#include <tactility/log.h>

#include <dummy_codec.h>
#include <esp_codec_dev.h>

#define TAG "PdmMic"

namespace {

// PDM mics have no fixed native rate of their own -- the PDM RX peripheral generates
// whatever clock the configured sample rate implies. Report a common voice-capture
// default; audio_stream resamples to whatever rate it is opened with.
constexpr uint32_t NATIVE_SAMPLE_RATE = 16000;

struct PdmMicData {
    const audio_codec_data_if_t* dataIf = nullptr;
    const audio_codec_if_t* codecIf = nullptr;
    esp_codec_dev_handle_t codecDevice = nullptr;
    uint8_t nativeChannels = 1;
    float inputGain = 1.0f;
    bool isOpen = false;
    esp_codec_dev_sample_info_t openSampleInfo = {};
};

#define GET_CONFIG(device) (static_cast<const PdmMicConfig*>((device)->config))
#define GET_DATA(device) (static_cast<PdmMicData*>(device_get_driver_data(device)))

// region AudioCodecApi

error_t open_(Device* device, const struct AudioCodecStreamConfig* config) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr) {
        return ERROR_RESOURCE;
    }

    // Input-only codec (see getCapabilities()/write_()) -- BOTH would imply output support
    // that write_() can't actually provide.
    if (config->direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    esp_codec_dev_sample_info_t sampleInfo = {
        .bits_per_sample = config->bits_per_sample,
        .channel = config->channels,
        .channel_mask = 0,
        .sample_rate = config->sample_rate,
        .mclk_multiple = 0,
    };

    if (data->isOpen) {
        bool sameConfig = data->openSampleInfo.bits_per_sample == sampleInfo.bits_per_sample
            && data->openSampleInfo.channel == sampleInfo.channel
            && data->openSampleInfo.sample_rate == sampleInfo.sample_rate;
        return sameConfig ? ERROR_NONE : ERROR_RESOURCE;
    }

    if (esp_codec_dev_open(data->codecDevice, &sampleInfo) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open codec device");
        return ERROR_RESOURCE;
    }

    data->isOpen = true;
    data->openSampleInfo = sampleInfo;
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

    // esp_codec_dev_read returns the number of bytes read (>= 0) on success, or a negative
    // ESP_CODEC_DEV_* error code on failure -- it does NOT return ESP_CODEC_DEV_OK (0) for
    // a successful nonzero-length read.
    int result = esp_codec_dev_read(data->codecDevice, buffer, (int) size);
    if (result < 0) {
        return ERROR_RESOURCE;
    }

    if (bytesRead != nullptr) {
        *bytesRead = (size_t) result;
    }
    return ERROR_NONE;
}

error_t write_(Device* device, const void* data_, size_t size, size_t* bytesWritten, TickType_t timeout) {
    (void) device;
    (void) data_;
    (void) size;
    (void) bytesWritten;
    (void) timeout;
    return ERROR_NOT_SUPPORTED;
}

// PDM mics have no hardware gain/mute registers, and unlike esp_codec_dev's output path
// (esp_codec_dev_set_out_vol(), which falls back to a software volume handler when the
// codec_if has no set_vol), the input path (esp_codec_dev_set_in_gain()/set_in_mute()) has
// no such fallback -- it returns ESP_CODEC_DEV_NOT_SUPPORT outright when the codec_if's
// set_mic_gain/mute_mic are NULL, which dummy_codec always leaves NULL. So input
// volume/mute genuinely isn't supported through this codec for now; report that honestly
// rather than masking a dead call path.

error_t setVolume(Device* device, enum AudioCodecDirection direction, float volumePercent) {
    (void) device;
    (void) volumePercent;
    if (direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    return ERROR_NOT_SUPPORTED;
}

error_t getVolume(Device* device, enum AudioCodecDirection direction, float* volumePercent) {
    (void) device;
    (void) volumePercent;
    if (direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    return ERROR_NOT_SUPPORTED;
}

error_t setMute(Device* device, enum AudioCodecDirection direction, bool muted) {
    (void) device;
    (void) muted;
    if (direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    return ERROR_NOT_SUPPORTED;
}

error_t getMute(Device* device, enum AudioCodecDirection direction, bool* muted) {
    (void) device;
    (void) muted;
    if (direction != AUDIO_CODEC_DIR_INPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    return ERROR_NOT_SUPPORTED;
}

error_t getNativeChannels(Device* device, enum AudioCodecDirection direction, uint8_t* channels) {
    auto* data = GET_DATA(device);
    if (data == nullptr || direction != AUDIO_CODEC_DIR_INPUT || channels == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *channels = data->nativeChannels;
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

    if (config->channels == 0 || config->channels > 2) {
        LOG_E(TAG, "Invalid channel count %u (must be 1 or 2)", config->channels);
        return ERROR_RESOURCE;
    }

    // devicetree's int property type has no min/max constraint support, so a bogus
    // negative literal (e.g. "input-gain-x100 = <-1>;") would otherwise silently wrap to
    // 65535 when narrowed to uint16_t and produce a wildly wrong gain. Bound it here
    // instead -- 0 disables boost entirely, 2000 (20x) is already an extreme upper bound
    // for compensating a quiet mic capsule.
    if (config->input_gain_x100 > 2000) {
        LOG_E(TAG, "Invalid input_gain_x100 %u (must be 0..2000)", config->input_gain_x100);
        return ERROR_RESOURCE;
    }

    auto* i2sController = device_find_by_name(config->i2s_device_name);
    if (i2sController == nullptr || device_get_type(i2sController) != &I2S_CONTROLLER_TYPE) {
        LOG_E(TAG, "I2S controller '%s' not found", config->i2s_device_name);
        return ERROR_RESOURCE;
    }

    auto* data = new PdmMicData();
    data->nativeChannels = config->channels;
    data->inputGain = (float) config->input_gain_x100 / 100.0f;

    data->dataIf = audio_codec_adapter_new_i2s_pdm_data(i2sController);
    if (data->dataIf == nullptr) {
        LOG_E(TAG, "Failed to create I2S PDM data adapter");
        goto cleanup;
    }

    if (data->dataIf->open(data->dataIf, nullptr, 0) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open data interface");
        goto cleanup;
    }

    {
        dummy_codec_cfg_t codecConfig = {
            .gpio_if = nullptr,
            .pa_pin = -1,
            .pa_reverted = false,
        };

        data->codecIf = dummy_codec_new(&codecConfig);
        if (data->codecIf == nullptr) {
            LOG_E(TAG, "Failed to create dummy codec interface");
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

    device_set_driver_data(device, nullptr);
    delete data;
    return ERROR_NONE;
}

// endregion

} // namespace

extern "C" {

Driver pdm_mic_driver = {
    .name = "pdm_mic",
    .compatible = (const char*[]) { "generic,spm1423", nullptr },
    .start_device = startDevice,
    .stop_device = stopDevice,
    .api = &API,
    .device_type = &AUDIO_CODEC_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

}
