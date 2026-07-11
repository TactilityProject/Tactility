// SPDX-License-Identifier: Apache-2.0
#include <esp_adc/adc_oneshot.h>

#include <tactility/error_esp32.h>
#include <tactility/driver.h>
#include <tactility/drivers/adc_controller.h>
#include <tactility/drivers/esp32_adc_oneshot.h>
#include <tactility/log.h>
#include <tactility/time.h>

#define TAG "esp32_adc_oneshot"

#define GET_CONFIG(device) ((Esp32AdcOneshotConfig*)device->config)
#define GET_HANDLE(device) ((adc_oneshot_unit_handle_t)device_get_driver_data(device))

static const Esp32AdcOneshotChannelConfig* find_channel_config(const Esp32AdcOneshotConfig* dts_config, uint8_t channel_index) {
    if (channel_index >= dts_config->channel_count) {
        return nullptr;
    }
    return &dts_config->channels[channel_index];
}

extern "C" {

static error_t read_raw(Device* device, uint8_t channel, int* out_raw, TickType_t timeout) {
    if (xPortInIsrContext()) return ERROR_ISR_STATUS;
    auto* dts_config = GET_CONFIG(device);
    auto* channel_config = find_channel_config(dts_config, channel);
    if (channel_config == nullptr) {
        return ERROR_OUT_OF_RANGE;
    }

    esp_err_t esp_error = adc_oneshot_read(GET_HANDLE(device), channel_config->channel, out_raw);
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "read(channel=%u) failed: %s", channel, esp_err_to_name(esp_error));
    }
    return esp_err_to_error(esp_error);
}

static error_t start(Device* device) {
    LOG_I(TAG, "start %s", device->name);
    auto* dts_config = GET_CONFIG(device);

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = dts_config->unit_id,
        .clk_src = dts_config->clk_src,
        .ulp_mode = dts_config->ulp_mode,
    };

    adc_oneshot_unit_handle_t handle;
    esp_err_t esp_error = adc_oneshot_new_unit(&init_config, &handle);
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "Failed to create ADC unit %d: %s", (int)dts_config->unit_id, esp_err_to_name(esp_error));
        return ERROR_RESOURCE;
    }

    for (size_t i = 0; i < dts_config->channel_count; i++) {
        const auto& channel_config = dts_config->channels[i];
        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten = channel_config.atten,
            .bitwidth = channel_config.bitwidth,
        };
        esp_error = adc_oneshot_config_channel(handle, channel_config.channel, &chan_cfg);
        if (esp_error != ESP_OK) {
            LOG_E(TAG, "Failed to configure channel %d: %s", (int)channel_config.channel, esp_err_to_name(esp_error));
            adc_oneshot_del_unit(handle);
            return ERROR_RESOURCE;
        }
    }

    device_set_driver_data(device, handle);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    LOG_I(TAG, "stop %s", device->name);
    adc_oneshot_del_unit(GET_HANDLE(device));
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

static constexpr AdcControllerApi ESP32_ADC_ONESHOT_API = {
    .read_raw = read_raw
};

extern Module platform_esp32_module;

Driver esp32_adc_oneshot_driver = {
    .name = "esp32_adc_oneshot",
    .compatible = (const char*[]) { "espressif,esp32-adc-oneshot", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &ESP32_ADC_ONESHOT_API,
    .device_type = &ADC_CONTROLLER_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};

} // extern "C"
