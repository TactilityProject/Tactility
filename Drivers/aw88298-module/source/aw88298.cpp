// SPDX-License-Identifier: Apache-2.0
#include <drivers/aw88298.h>

#include <tactility/device.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/drivers/audio_codec_adapters.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/i2s_controller.h>
#include <tactility/log.h>

#include <aw88298_dac.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <driver/i2s_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cmath>

#define TAG "AW88298"

namespace {

// AW88298 native output rate per M5Unified's _speaker_enabled_cb_cores3(); higher rates
// (e.g. 44100Hz) additionally need mclk_multiple = I2S_MCLK_MULTIPLE_384 per aw88298_dac.h.
constexpr uint32_t NATIVE_SAMPLE_RATE = 16000;

struct Aw88298Data {
    const audio_codec_ctrl_if_t* ctrlIf = nullptr;
    const audio_codec_data_if_t* dataIf = nullptr;
    const audio_codec_if_t* codecIf = nullptr;
    esp_codec_dev_handle_t codecDevice = nullptr;
    bool isOpen = false;
    esp_codec_dev_sample_info_t openSampleInfo = {};
};

#define GET_CONFIG(device) (static_cast<const Aw88298Config*>((device)->config))
#define GET_DATA(device) (static_cast<Aw88298Data*>(device_get_driver_data(device)))

// region AudioCodecApi

error_t open_(Device* device, const struct AudioCodecStreamConfig* config) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr) {
        return ERROR_RESOURCE;
    }

    if (config->direction == AUDIO_CODEC_DIR_INPUT || config->direction == AUDIO_CODEC_DIR_BOTH) {
        LOG_E(TAG, "AW88298 is output-only");
        return ERROR_NOT_SUPPORTED;
    }

    esp_codec_dev_sample_info_t sampleInfo = {
        .bits_per_sample = config->bits_per_sample,
        .channel = config->channels,
        .channel_mask = 0,
        .sample_rate = config->sample_rate,
        .mclk_multiple = (config->sample_rate > 16000) ? I2S_MCLK_MULTIPLE_384 : 0,
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
    (void) device;
    (void) buffer;
    (void) size;
    (void) bytesRead;
    (void) timeout;
    return ERROR_NOT_SUPPORTED;
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
    if (data == nullptr || data->codecDevice == nullptr || direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    if (volumePercent < 0.0f || volumePercent > 100.0f) {
        return ERROR_RESOURCE;
    }

    int volume = (int) std::lround(volumePercent);
    return (esp_codec_dev_set_out_vol(data->codecDevice, volume) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t getVolume(Device* device, enum AudioCodecDirection direction, float* volumePercent) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr || direction != AUDIO_CODEC_DIR_OUTPUT || volumePercent == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    int volume = 0;
    if (esp_codec_dev_get_out_vol(data->codecDevice, &volume) != ESP_CODEC_DEV_OK) {
        return ERROR_RESOURCE;
    }
    *volumePercent = (float) volume;
    return ERROR_NONE;
}

error_t setMute(Device* device, enum AudioCodecDirection direction, bool muted) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr || direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    return (esp_codec_dev_set_out_mute(data->codecDevice, muted) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t getMute(Device* device, enum AudioCodecDirection direction, bool* muted) {
    auto* data = GET_DATA(device);
    if (data == nullptr || data->codecDevice == nullptr || direction != AUDIO_CODEC_DIR_OUTPUT || muted == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    return (esp_codec_dev_get_out_mute(data->codecDevice, muted) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t getNativeChannels(Device* device, enum AudioCodecDirection direction, uint8_t* channels) {
    (void) device;
    if (direction != AUDIO_CODEC_DIR_OUTPUT || channels == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *channels = 2;
    return ERROR_NONE;
}

error_t getNativeSampleRate(Device* device, enum AudioCodecDirection direction, uint32_t* rateHz) {
    (void) device;
    if (direction != AUDIO_CODEC_DIR_OUTPUT || rateHz == nullptr) {
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
    *supportedDirections = AUDIO_CODEC_DIR_OUTPUT;
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

    auto* data = new Aw88298Data();

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

    // If a reset pin is wired (e.g. behind a GPIO expander), the AW88298's I2C interface
    // won't respond until it's released. We pulse it ourselves here rather than handing it
    // to aw88298_codec_new() via gpio_if/reset_pin: the vendor driver's internal
    // aw88298_reset() treats reset_pin <= 0 as "no pin wired" and no-ops (aw88298.c:197),
    // but the audio_codec_adapter_new_gpio() convention (matched by dummy_i2s_amp's
    // pa_pin, see Drivers/dummy-i2s-amp-module) is to pass 0 as the array index of a
    // single-pin GPioPinSpec array -- so index 0 collides with the vendor's own "disabled"
    // sentinel and the reset never actually fires. This runs synchronously as part of this
    // device's own startDevice(), so it's correctly ordered relative to devicetree bring-up
    // (unlike a board Configuration.cpp initBoot() callback, which only runs after ALL
    // devicetree devices, including this one, have already started).
    if (config->reset_pin.gpio_controller != nullptr) {
        auto* resetDescriptor = gpio_descriptor_acquire(config->reset_pin.gpio_controller, config->reset_pin.pin, GPIO_OWNER_GPIO);
        if (resetDescriptor == nullptr) {
            LOG_E(TAG, "Failed to acquire reset pin descriptor");
            goto cleanup;
        }
        gpio_descriptor_set_flags(resetDescriptor, GPIO_FLAG_DIRECTION_OUTPUT);
        gpio_descriptor_set_level(resetDescriptor, false);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_descriptor_set_level(resetDescriptor, true);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_descriptor_release(resetDescriptor);
    }

    {
        aw88298_codec_cfg_t codecConfig = {};
        codecConfig.ctrl_if = data->ctrlIf;
        codecConfig.gpio_if = nullptr;
        codecConfig.reset_pin = -1;
        codecConfig.hw_gain = {};

        data->codecIf = aw88298_codec_new(&codecConfig);
        if (data->codecIf == nullptr) {
            LOG_E(TAG, "Failed to create AW88298 codec interface");
            goto cleanup;
        }
    }

    {
        esp_codec_dev_cfg_t devConfig = {
            .dev_type = ESP_CODEC_DEV_TYPE_OUT,
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

Driver aw88298_driver = {
    .name = "aw88298",
    .compatible = (const char*[]) { "awinic,aw88298", nullptr },
    .start_device = startDevice,
    .stop_device = stopDevice,
    .api = &API,
    .device_type = &AUDIO_CODEC_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

}
