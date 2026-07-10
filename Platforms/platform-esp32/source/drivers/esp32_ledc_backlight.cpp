// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/esp32_ledc_backlight.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/backlight.h>
#include <tactility/log.h>

#include <driver/ledc.h>
#include <esp_err.h>

#include <cstdlib>

#define TAG "Esp32LedcBacklight"
#define GET_CONFIG(device) (static_cast<const Esp32LedcBacklightConfig*>((device)->config))

struct Esp32LedcBacklightInternal {
    uint8_t brightness;
};

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = config->ledc_timer,
        .freq_hz = config->frequency_hz,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    if (ledc_timer_config(&timer_config) != ESP_OK) {
        LOG_E(TAG, "Failed to configure LEDC timer");
        return ERROR_RESOURCE;
    }

    ledc_channel_config_t channel_config = {
        .gpio_num = (int)config->pin_backlight.pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = config->ledc_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = config->ledc_timer,
        .duty = config->brightness_range.min,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = 0,
        },
    };
    if (ledc_channel_config(&channel_config) != ESP_OK) {
        LOG_E(TAG, "Failed to configure LEDC channel");
        return ERROR_RESOURCE;
    }

    auto* internal = static_cast<Esp32LedcBacklightInternal*>(malloc(sizeof(Esp32LedcBacklightInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->brightness = config->brightness_range.min;

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Esp32LedcBacklightInternal*>(device_get_driver_data(device));
    free(internal);
    return ERROR_NONE;
}

// endregion

// region BacklightApi

static error_t esp32_ledc_backlight_set_brightness(Device* device, uint8_t brightness) {
    const auto* config = GET_CONFIG(device);
    auto* internal = static_cast<Esp32LedcBacklightInternal*>(device_get_driver_data(device));

    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, config->ledc_channel, brightness);
    if (ret == ESP_OK) {
        ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, config->ledc_channel);
    }
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to set brightness: %s", esp_err_to_name(ret));
        return ERROR_RESOURCE;
    }

    internal->brightness = brightness;
    return ERROR_NONE;
}

static error_t esp32_ledc_backlight_set_brightness_default(Device* device) {
    return esp32_ledc_backlight_set_brightness(device, GET_CONFIG(device)->brightness_default);
}

static error_t esp32_ledc_backlight_get_brightness(Device* device, uint8_t* out_brightness) {
    auto* internal = static_cast<Esp32LedcBacklightInternal*>(device_get_driver_data(device));
    *out_brightness = internal->brightness;
    return ERROR_NONE;
}

static uint8_t esp32_ledc_backlight_get_min_brightness(Device* device) {
    return GET_CONFIG(device)->brightness_range.min;
}

static uint8_t esp32_ledc_backlight_get_max_brightness(Device* device) {
    return GET_CONFIG(device)->brightness_range.max;
}

// endregion

static const BacklightApi esp32_ledc_backlight_api = {
    .set_brightness = esp32_ledc_backlight_set_brightness,
    .set_brightness_default = esp32_ledc_backlight_set_brightness_default,
    .get_brightness = esp32_ledc_backlight_get_brightness,
    .get_min_brightness = esp32_ledc_backlight_get_min_brightness,
    .get_max_brightness = esp32_ledc_backlight_get_max_brightness,
};

extern Module platform_esp32_module;

Driver esp32_ledc_backlight_driver = {
    .name = "esp32_ledc_backlight",
    .compatible = (const char*[]) { "espressif,esp32-ledc-backlight", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &esp32_ledc_backlight_api,
    .device_type = &BACKLIGHT_TYPE,
    .owner = &platform_esp32_module,
    .internal = nullptr
};
