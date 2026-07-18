// SPDX-License-Identifier: Apache-2.0
#include <drivers/axp192_backlight.h>
#include <axp192_module.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/backlight.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <new>

constexpr auto* TAG = "Axp192Backlight";

#define GET_CONFIG(device) (static_cast<const Axp192BacklightConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<Axp192BacklightInternal*>(device_get_driver_data(device)))

extern "C" {

struct Axp192BacklightInternal {
    uint8_t brightness;
};

// region BacklightApi

static error_t apply_brightness(Device* device, uint8_t brightness) {
    const auto* config = GET_CONFIG(device);
    auto* axp192 = device_get_parent(device);

    if (brightness == 0) {
        return axp192_set_rail_enabled(axp192, config->rail, false);
    }

    uint16_t millivolt = static_cast<uint16_t>(
        config->min_millivolt +
        (static_cast<uint32_t>(brightness) * (config->max_millivolt - config->min_millivolt)) / 255U
    );

    error_t error = axp192_set_rail_voltage(axp192, config->rail, millivolt);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to set rail voltage");
        return error;
    }

    return axp192_set_rail_enabled(axp192, config->rail, true);
}

static error_t axp192_backlight_set_brightness(Device* device, uint8_t brightness) {
    error_t error = apply_brightness(device, brightness);
    if (error != ERROR_NONE) {
        return error;
    }
    GET_INTERNAL(device)->brightness = brightness;
    return ERROR_NONE;
}

static error_t axp192_backlight_set_brightness_default(Device* device) {
    return axp192_backlight_set_brightness(device, GET_CONFIG(device)->brightness_default);
}

static error_t axp192_backlight_get_brightness(Device* device, uint8_t* out_brightness) {
    *out_brightness = GET_INTERNAL(device)->brightness;
    return ERROR_NONE;
}

static uint8_t axp192_backlight_get_min_brightness(Device*) {
    return 0;
}

static uint8_t axp192_backlight_get_max_brightness(Device*) {
    return 255;
}

// endregion

static constexpr BacklightApi AXP192_BACKLIGHT_API = {
    .set_brightness = axp192_backlight_set_brightness,
    .set_brightness_default = axp192_backlight_set_brightness_default,
    .get_brightness = axp192_backlight_get_brightness,
    .get_min_brightness = axp192_backlight_get_min_brightness,
    .get_max_brightness = axp192_backlight_get_max_brightness,
};

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);
    if (config->max_millivolt <= config->min_millivolt) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* internal = new(std::nothrow) Axp192BacklightInternal { .brightness = 0 };
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    device_set_driver_data(device, internal);

    error_t error = axp192_backlight_set_brightness_default(device);
    if (error != ERROR_NONE) {
        device_set_driver_data(device, nullptr);
        delete internal;
        return error;
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    axp192_backlight_set_brightness(device, 0); // Allowed to fail, we don't care about the result

    auto* internal = GET_INTERNAL(device);
    device_set_driver_data(device, nullptr);
    delete internal;

    return ERROR_NONE;
}

// endregion

Driver axp192_backlight_driver = {
    .name = "axp192_backlight",
    .compatible = (const char*[]) { "axp192-backlight", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &AXP192_BACKLIGHT_API,
    .device_type = &BACKLIGHT_TYPE,
    .owner = &axp192_module,
    .internal = nullptr
};

}
