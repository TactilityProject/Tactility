// SPDX-License-Identifier: Apache-2.0
#include <drivers/es8388.h>

#include <tactility/device.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/drivers/audio_codec_adapters.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/i2s_controller.h>
#include <tactility/log.h>

#include <es8388_codec.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>

#include <cmath>

#define TAG "ES8388"

namespace {

// Volume curve maps 1..100 to roughly -49.5dB..0dB; native ES8388 range is wide enough
// that we expose a flat 0..100 percent scale to callers and let esp_codec_dev convert.
constexpr uint32_t NATIVE_SAMPLE_RATE = 44100;

struct Es8388Data {
    const audio_codec_ctrl_if_t* ctrlIf = nullptr;
    const audio_codec_data_if_t* dataIf = nullptr;
    const audio_codec_gpio_if_t* gpioIf = nullptr;
    const audio_codec_if_t* codecIf = nullptr;
    esp_codec_dev_handle_t codecDevice = nullptr;
    bool isOpen = false;
    enum AudioCodecDirection openDirection = AUDIO_CODEC_DIR_BOTH;
    esp_codec_dev_sample_info_t openSampleInfo = {};
};

#define GET_CONFIG(device) (static_cast<const Es8388Config*>((device)->config))
#define GET_DATA(device) (static_cast<Es8388Data*>(device_get_driver_data(device)))

// region AudioCodecApi

error_t open_(Device* device, const struct AudioCodecStreamConfig* config) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr) {
        return ERROR_RESOURCE;
    }

    esp_codec_dev_sample_info_t sampleInfo = {
        .bits_per_sample = config->bits_per_sample,
        .channel = config->channels,
        .channel_mask = 0,
        .sample_rate = config->sample_rate,
        .mclk_multiple = 0,
    };

    if (data->isOpen) {
        // openDirection == BOTH already serves INPUT-only or OUTPUT-only requests on the
        // same sample settings -- only an exact direction mismatch (e.g. requesting BOTH
        // while opened for INPUT only) needs a reopen.
        bool directionCompatible = data->openDirection == config->direction
            || data->openDirection == AUDIO_CODEC_DIR_BOTH;
        bool sameConfig = directionCompatible
            && data->openSampleInfo.bits_per_sample == sampleInfo.bits_per_sample
            && data->openSampleInfo.channel == sampleInfo.channel
            && data->openSampleInfo.sample_rate == sampleInfo.sample_rate;
        return sameConfig ? ERROR_NONE : ERROR_RESOURCE;
    }

    if (esp_codec_dev_open(data->codecDevice, &sampleInfo) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open codec device");
        return ERROR_RESOURCE;
    }

    data->isOpen = true;
    data->openDirection = config->direction;
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

error_t write_(Device* device, const void* buffer, size_t size, size_t* bytesWritten, TickType_t timeout) {
    (void) timeout;
    auto* data = GET_DATA(device);
    if (data == nullptr || !data->isOpen) {
        return ERROR_RESOURCE;
    }

    // esp_codec_dev_write returns the number of bytes written (>= 0) on success, or a negative
    // ESP_CODEC_DEV_* error code on failure -- it does NOT return ESP_CODEC_DEV_OK (0) for
    // a successful nonzero-length write.
    int result = esp_codec_dev_write(data->codecDevice, const_cast<void*>(buffer), (int) size);
    if (result < 0) {
        return ERROR_RESOURCE;
    }

    if (bytesWritten != nullptr) {
        *bytesWritten = (size_t) result;
    }
    return ERROR_NONE;
}

error_t setVolume(Device* device, enum AudioCodecDirection direction, float volumePercent) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr) {
        return ERROR_RESOURCE;
    }

    if (volumePercent < 0.0f || volumePercent > 100.0f) {
        return ERROR_RESOURCE;
    }

    if (direction == AUDIO_CODEC_DIR_OUTPUT) {
        int volume = (int) std::lround(volumePercent);
        return (esp_codec_dev_set_out_vol(data->codecDevice, volume) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
    }

    if (direction == AUDIO_CODEC_DIR_INPUT) {
        // ES8388 ADC gain range is roughly 0..24 dB; map 0..100% linearly onto it.
        float db = (volumePercent / 100.0f) * 24.0f;
        return (esp_codec_dev_set_in_gain(data->codecDevice, db) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
    }

    return ERROR_NOT_SUPPORTED;
}

error_t getVolume(Device* device, enum AudioCodecDirection direction, float* volumePercent) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr || volumePercent == nullptr) {
        return ERROR_RESOURCE;
    }

    if (direction == AUDIO_CODEC_DIR_OUTPUT) {
        int volume = 0;
        if (esp_codec_dev_get_out_vol(data->codecDevice, &volume) != ESP_CODEC_DEV_OK) {
            return ERROR_RESOURCE;
        }
        *volumePercent = (float) volume;
        return ERROR_NONE;
    }

    if (direction == AUDIO_CODEC_DIR_INPUT) {
        float db = 0.0f;
        if (esp_codec_dev_get_in_gain(data->codecDevice, &db) != ESP_CODEC_DEV_OK) {
            return ERROR_RESOURCE;
        }
        *volumePercent = (db / 24.0f) * 100.0f;
        return ERROR_NONE;
    }

    return ERROR_NOT_SUPPORTED;
}

error_t setMute(Device* device, enum AudioCodecDirection direction, bool muted) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr) {
        return ERROR_RESOURCE;
    }

    if (direction == AUDIO_CODEC_DIR_OUTPUT) {
        return (esp_codec_dev_set_out_mute(data->codecDevice, muted) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
    }

    if (direction == AUDIO_CODEC_DIR_INPUT) {
        return (esp_codec_dev_set_in_mute(data->codecDevice, muted) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
    }

    return ERROR_NOT_SUPPORTED;
}

error_t getMute(Device* device, enum AudioCodecDirection direction, bool* muted) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr || muted == nullptr) {
        return ERROR_RESOURCE;
    }

    if (direction == AUDIO_CODEC_DIR_OUTPUT) {
        return (esp_codec_dev_get_out_mute(data->codecDevice, muted) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
    }

    if (direction == AUDIO_CODEC_DIR_INPUT) {
        return (esp_codec_dev_get_in_mute(data->codecDevice, muted) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
    }

    return ERROR_NOT_SUPPORTED;
}

error_t getNativeChannels(Device* device, enum AudioCodecDirection direction, uint8_t* channels) {
    (void) device;
    if ((direction != AUDIO_CODEC_DIR_INPUT && direction != AUDIO_CODEC_DIR_OUTPUT) || channels == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *channels = 2;
    return ERROR_NONE;
}

error_t getNativeSampleRate(Device* device, enum AudioCodecDirection direction, uint32_t* rateHz) {
    (void) device;
    (void) direction;
    if (rateHz == nullptr) {
        return ERROR_RESOURCE;
    }
    *rateHz = NATIVE_SAMPLE_RATE;
    return ERROR_NONE;
}

error_t getCapabilities(Device* device, enum AudioCodecDirection* supportedDirections) {
    (void) device;
    if (supportedDirections == nullptr) {
        return ERROR_RESOURCE;
    }
    *supportedDirections = AUDIO_CODEC_DIR_BOTH;
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
    .get_input_gain_multiplier = nullptr,
};

// endregion

// region Driver lifecycle

error_t startDevice(Device* device) {
    const auto* config = GET_CONFIG(device);

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

    auto* data = new Es8388Data();

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
        es8388_codec_cfg_t codecConfig = {};
        codecConfig.ctrl_if = data->ctrlIf;
        codecConfig.gpio_if = nullptr;
        codecConfig.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
        codecConfig.master_mode = false;
        codecConfig.pa_pin = -1;
        codecConfig.pa_reverted = false;

        data->codecIf = es8388_codec_new(&codecConfig);
        if (data->codecIf == nullptr) {
            LOG_E(TAG, "Failed to create ES8388 codec interface");
            goto cleanup;
        }
    }

    {
        esp_codec_dev_cfg_t devConfig = {
            .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
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
    if (data->gpioIf != nullptr) {
        audio_codec_adapter_delete_gpio(data->gpioIf);
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
    if (data->gpioIf != nullptr) {
        audio_codec_adapter_delete_gpio(data->gpioIf);
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

Driver es8388_driver = {
    .name = "es8388",
    .compatible = (const char*[]) { "everest,es8388", nullptr },
    .start_device = startDevice,
    .stop_device = stopDevice,
    .api = &API,
    .device_type = &AUDIO_CODEC_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

}
