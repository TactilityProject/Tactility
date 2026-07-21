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
    const audio_codec_ctrl_if_t* ctrl_if = nullptr;
    const audio_codec_data_if_t* data_if = nullptr;
    const audio_codec_if_t* codec_if = nullptr;
    esp_codec_dev_handle_t codec_device = nullptr;
    bool is_open = false;
    esp_codec_dev_sample_info_t open_sample_info = {};
};

#define GET_CONFIG(device) (static_cast<const Aw88298Config*>((device)->config))
#define GET_DATA(device) (static_cast<Aw88298Data*>(device_get_driver_data(device)))

// region AudioCodecApi

error_t open(Device* device, const struct AudioCodecStreamConfig* config) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr) {
        return ERROR_RESOURCE;
    }

    if (config->direction == AUDIO_CODEC_DIR_INPUT || config->direction == AUDIO_CODEC_DIR_BOTH) {
        LOG_E(TAG, "AW88298 is output-only");
        return ERROR_NOT_SUPPORTED;
    }

    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = config->bits_per_sample,
        .channel = config->channels,
        .channel_mask = 0,
        .sample_rate = config->sample_rate,
        .mclk_multiple = (config->sample_rate > 16000) ? I2S_MCLK_MULTIPLE_384 : 0,
    };

    if (data->is_open) {
        bool same_config = data->open_sample_info.bits_per_sample == sample_info.bits_per_sample
            && data->open_sample_info.channel == sample_info.channel
            && data->open_sample_info.sample_rate == sample_info.sample_rate;
        return same_config ? ERROR_NONE : ERROR_RESOURCE;
    }

    if (esp_codec_dev_open(data->codec_device, &sample_info) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open codec device");
        return ERROR_RESOURCE;
    }

    data->is_open = true;
    data->open_sample_info = sample_info;
    return ERROR_NONE;
}

error_t close(Device* device) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr) {
        return ERROR_RESOURCE;
    }

    if (data->is_open) {
        esp_codec_dev_close(data->codec_device);
        data->is_open = false;
    }

    return ERROR_NONE;
}

error_t read(Device* device, void* buffer, size_t size, size_t* bytes_read, TickType_t timeout) {
    (void) device;
    (void) buffer;
    (void) size;
    (void) bytes_read;
    (void) timeout;
    return ERROR_NOT_SUPPORTED;
}

error_t write(Device* device, const void* buffer, size_t size, size_t* bytes_written, TickType_t timeout) {
    (void) timeout;
    auto* data = GET_DATA(device);
    if (!data->is_open) {
        return ERROR_RESOURCE;
    }

    // esp_codec_dev_write returns the number of bytes written (>= 0) on success, or a negative
    // ESP_CODEC_DEV_* error code on failure -- it does NOT return ESP_CODEC_DEV_OK (0) for
    // a successful nonzero-length write.
    int result = esp_codec_dev_write(data->codec_device, const_cast<void*>(buffer), (int) size);
    if (result < 0) {
        return ERROR_RESOURCE;
    }

    *bytes_written = (size_t) result;
    return ERROR_NONE;
}

error_t set_volume(Device* device, AudioCodecDirection direction, float volume_percent) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr || direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    if (volume_percent < 0.0f || volume_percent > 100.0f) {
        return ERROR_RESOURCE;
    }

    int volume = (int) std::lround(volume_percent);
    return (esp_codec_dev_set_out_vol(data->codec_device, volume) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t get_volume(Device* device, AudioCodecDirection direction, float* volume_percent) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr || direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    int volume = 0;
    if (esp_codec_dev_get_out_vol(data->codec_device, &volume) != ESP_CODEC_DEV_OK) {
        return ERROR_RESOURCE;
    }
    *volume_percent = (float) volume;
    return ERROR_NONE;
}

error_t set_mute(Device* device, AudioCodecDirection direction, bool muted) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr || direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    return (esp_codec_dev_set_out_mute(data->codec_device, muted) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t get_mute(Device* device, AudioCodecDirection direction, bool* muted) {
    auto* data = GET_DATA(device);
    if (data->codec_device == nullptr || direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }

    return (esp_codec_dev_get_out_mute(data->codec_device, muted) == ESP_CODEC_DEV_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

error_t get_native_channels(Device* device, AudioCodecDirection direction, uint8_t* channels) {
    (void) device;
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    *channels = 2;
    return ERROR_NONE;
}

error_t get_native_sample_rate(Device* device, AudioCodecDirection direction, uint32_t* rate_hz) {
    (void) device;
    if (direction != AUDIO_CODEC_DIR_OUTPUT) {
        return ERROR_NOT_SUPPORTED;
    }
    *rate_hz = NATIVE_SAMPLE_RATE;
    return ERROR_NONE;
}

error_t get_capabilities(Device* device, AudioCodecDirection* supported_directions) {
    (void) device;
    *supported_directions = AUDIO_CODEC_DIR_OUTPUT;
    return ERROR_NONE;
}

static const struct AudioCodecApi API = {
    .open = open,
    .close = close,
    .read = read,
    .write = write,
    .set_volume = set_volume,
    .get_volume = get_volume,
    .set_mute = set_mute,
    .get_mute = get_mute,
    .get_native_sample_rate = get_native_sample_rate,
    .get_native_channels = get_native_channels,
    .get_capabilities = get_capabilities,
    .get_input_gain_multiplier = nullptr,
};

// endregion

// region Driver lifecycle

error_t start_device(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* i2c_controller = device_get_parent(device);
    if (i2c_controller == nullptr || device_get_type(i2c_controller) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto* i2s_controller = config->i2s_device;
    if (i2s_controller == nullptr || device_get_type(i2s_controller) != &I2S_CONTROLLER_TYPE) {
        LOG_E(TAG, "I2S controller device is not valid");
        return ERROR_RESOURCE;
    }

    auto* data = new Aw88298Data();

    data->ctrl_if = audio_codec_adapter_new_i2c_ctrl(i2c_controller, config->address);
    data->data_if = audio_codec_adapter_new_i2s_data(i2s_controller);
    if (data->ctrl_if == nullptr || data->data_if == nullptr) {
        LOG_E(TAG, "Failed to create adapters");
        goto cleanup;
    }

    if (data->ctrl_if->open(data->ctrl_if, nullptr, 0) != ESP_CODEC_DEV_OK) {
        LOG_E(TAG, "Failed to open control interface");
        goto cleanup;
    }

    if (data->data_if->open(data->data_if, nullptr, 0) != ESP_CODEC_DEV_OK) {
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
    // device's own start_device(), so it's correctly ordered relative to devicetree bring-up
    // (unlike a board Configuration.cpp initBoot() callback, which only runs after ALL
    // devicetree devices, including this one, have already started).
    if (config->reset_pin.gpio_controller != nullptr) {
        auto* reset_descriptor = gpio_descriptor_acquire(config->reset_pin.gpio_controller, config->reset_pin.pin, GPIO_FLAG_DIRECTION_OUTPUT | GPIO_FLAG_ACTIVE_LOW, GPIO_OWNER_GPIO);
        if (reset_descriptor == nullptr) {
            LOG_E(TAG, "Failed to acquire reset pin descriptor");
            goto cleanup;
        }
        gpio_descriptor_set_level(reset_descriptor, true);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_descriptor_set_level(reset_descriptor, false);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_descriptor_release(reset_descriptor);
    }

    {
        aw88298_codec_cfg_t codec_config = {};
        codec_config.ctrl_if = data->ctrl_if;
        codec_config.gpio_if = nullptr;
        codec_config.reset_pin = -1;
        codec_config.hw_gain = {};

        data->codec_if = aw88298_codec_new(&codec_config);
        if (data->codec_if == nullptr) {
            LOG_E(TAG, "Failed to create AW88298 codec interface");
            goto cleanup;
        }
    }

    {
        esp_codec_dev_cfg_t dev_config = {
            .dev_type = ESP_CODEC_DEV_TYPE_OUT,
            .codec_if = data->codec_if,
            .data_if = data->data_if,
        };

        data->codec_device = esp_codec_dev_new(&dev_config);
        if (data->codec_device == nullptr) {
            LOG_E(TAG, "Failed to create codec device");
            goto cleanup;
        }
    }

    device_set_driver_data(device, data);
    return ERROR_NONE;

cleanup:
    // Mirrors stop_device's teardown order -- delete_*_if() routines close their interface
    // first, so we don't need separate ->close() calls here.
    if (data->codec_device != nullptr) {
        esp_codec_dev_delete(data->codec_device);
    }
    if (data->codec_if != nullptr) {
        audio_codec_delete_codec_if(data->codec_if);
    }
    if (data->data_if != nullptr) {
        audio_codec_delete_data_if(data->data_if);
    }
    if (data->ctrl_if != nullptr) {
        audio_codec_delete_ctrl_if(data->ctrl_if);
    }
    delete data;
    return ERROR_RESOURCE;
}

error_t stop_device(Device* device) {
    auto* data = GET_DATA(device);
    if (data == nullptr) {
        return ERROR_NONE;
    }

    if (data->is_open) {
        esp_codec_dev_close(data->codec_device);
    }

    if (data->codec_device != nullptr) {
        esp_codec_dev_delete(data->codec_device);
    }
    if (data->codec_if != nullptr) {
        audio_codec_delete_codec_if(data->codec_if);
    }
    if (data->data_if != nullptr) {
        audio_codec_delete_data_if(data->data_if);
    }
    if (data->ctrl_if != nullptr) {
        audio_codec_delete_ctrl_if(data->ctrl_if);
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
    .start_device = start_device,
    .stop_device = stop_device,
    .api = &API,
    .device_type = &AUDIO_CODEC_TYPE,
    .owner = nullptr,
    .internal = nullptr,
};

}
