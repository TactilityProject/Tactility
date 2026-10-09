// SPDX-License-Identifier: Apache-2.0
#include "cl32_v4_light.h"
#include "cl32_v4.h"

#include <tactility/device.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>
#include <tactility/module.h>

extern Module cl32_v04_module;

constexpr auto* TAG = "cl32-v4-light";

#define GET_CONFIG(device) (static_cast<const Cl32V4LightConfig*>((device)->config))

static error_t set_brightness(Device* device, uint8_t brightness) {
    return i2c_controller_register8_set(device_get_parent(device), GET_CONFIG(device)->address, GET_CONFIG(device)->brightness_register, brightness, CL32_V4_CORE_TIMEOUT);
}

static error_t set_brightness_default(Device* device) {
    return set_brightness(device, GET_CONFIG(device)->brightness_default);
}

static error_t get_brightness(Device* device, uint8_t* out_brightness) {
    return i2c_controller_register8_get(device_get_parent(device), GET_CONFIG(device)->address, GET_CONFIG(device)->brightness_register, out_brightness, CL32_V4_CORE_TIMEOUT);
}

static constexpr BacklightApi CL32_V4_LIGHT_API = {
    .set_brightness = set_brightness,
    .set_brightness_default = set_brightness_default,
    .get_brightness = get_brightness,
    .get_min_brightness = nullptr,
    .get_max_brightness = nullptr,
};

static error_t start(Device* device) {
    auto* i2c0 = device_get_parent(device);
    if (device_get_type(i2c0) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    const auto* config = GET_CONFIG(device);
    if (config->timeout_seconds > 0) {
        auto timeout_value = config->timeout_seconds * 15;
        if (timeout_value > 255) {
            timeout_value = 255;
        }

        error_t error = i2c_controller_register8_set(i2c0, config->address, config->timeout_register, static_cast<uint8_t>(timeout_value), CL32_V4_CORE_TIMEOUT);
        if (error != ERROR_NONE) {
            LOG_E(TAG, "Failed to set timeout");
            return error;
        }
    }

    return ERROR_NONE;
}

static error_t stop(Device*) {
    return ERROR_NONE;
}

Driver cl32_v4_light_driver = {
    .name = "cl32-v4-light",
    .compatible = (const char*[]) { "cl32-v4-light", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &CL32_V4_LIGHT_API,
    .device_type = &BACKLIGHT_TYPE,
    .owner = &cl32_v04_module,
    .internal = nullptr,
};
