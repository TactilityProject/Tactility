// SPDX-License-Identifier: Apache-2.0
#include <drivers/axp2101_backlight.h>
#include <axp2101_module.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/backlight.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <new>

#define TAG "Axp2101Backlight"
#define GET_CONFIG(device) (static_cast<const Axp2101BacklightConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<Axp2101BacklightInternal*>(device_get_driver_data(device)))

extern "C" {

struct Axp2101BacklightInternal {
    uint8_t brightness;
};

// region BacklightApi

// Step size of axp2101_set_ldo_voltage()'s underlying register encoding (see axp2101.cpp's LDO_RANGE table).
static uint16_t get_ldo_voltage_step(Axp2101Ldo ldo) {
    return ldo == AXP2101_CPUSLDO ? 50U : 100U;
}

static error_t apply_brightness(Device* device, uint8_t brightness) {
    const auto* config = GET_CONFIG(device);
    auto* axp2101 = device_get_parent(device);

    if (brightness == 0) {
        return axp2101_set_ldo_enabled(axp2101, config->ldo, false);
    }

    uint16_t step = get_ldo_voltage_step(config->ldo);
    uint16_t raw_millivolt = static_cast<uint16_t>(
        config->min_millivolt +
        (static_cast<uint32_t>(brightness) * (config->max_millivolt - config->min_millivolt)) / 255U
    );
    // Round to the nearest valid step; the LDO's voltage range always starts at a multiple of every
    // supported step, so rounding from zero keeps the result on a valid boundary.
    uint16_t millivolt = static_cast<uint16_t>(((raw_millivolt + step / 2U) / step) * step);
    if (millivolt < config->min_millivolt) {
        millivolt = config->min_millivolt;
    } else if (millivolt > config->max_millivolt) {
        millivolt = config->max_millivolt;
    }

    error_t error = axp2101_set_ldo_voltage(axp2101, config->ldo, millivolt);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to set LDO voltage");
        return error;
    }

    return axp2101_set_ldo_enabled(axp2101, config->ldo, true);
}

static error_t axp2101_backlight_set_brightness(Device* device, uint8_t brightness) {
    error_t error = apply_brightness(device, brightness);
    if (error != ERROR_NONE) {
        return error;
    }
    GET_INTERNAL(device)->brightness = brightness;
    return ERROR_NONE;
}

static error_t axp2101_backlight_set_brightness_default(Device* device) {
    return axp2101_backlight_set_brightness(device, GET_CONFIG(device)->brightness_default);
}

static error_t axp2101_backlight_get_brightness(Device* device, uint8_t* out_brightness) {
    *out_brightness = GET_INTERNAL(device)->brightness;
    return ERROR_NONE;
}

static uint8_t axp2101_backlight_get_min_brightness(Device*) {
    return 0;
}

static uint8_t axp2101_backlight_get_max_brightness(Device*) {
    return 255;
}

// endregion

static constexpr BacklightApi AXP2101_BACKLIGHT_API = {
    .set_brightness = axp2101_backlight_set_brightness,
    .set_brightness_default = axp2101_backlight_set_brightness_default,
    .get_brightness = axp2101_backlight_get_brightness,
    .get_min_brightness = axp2101_backlight_get_min_brightness,
    .get_max_brightness = axp2101_backlight_get_max_brightness,
};

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);
    if (config->max_millivolt <= config->min_millivolt) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* internal = new(std::nothrow) Axp2101BacklightInternal { .brightness = 0 };
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    device_set_driver_data(device, internal);

    axp2101_backlight_set_brightness_default(device); // Allowed to fail, we don't care about the result

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    axp2101_backlight_set_brightness(device, 0); // Allowed to fail, we don't care about the result

    auto* internal = GET_INTERNAL(device);
    device_set_driver_data(device, nullptr);
    delete internal;

    return ERROR_NONE;
}

// endregion

Driver axp2101_backlight_driver = {
    .name = "axp2101_backlight",
    .compatible = (const char*[]) { "axp2101-backlight", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &AXP2101_BACKLIGHT_API,
    .device_type = &BACKLIGHT_TYPE,
    .owner = &axp2101_module,
    .internal = nullptr
};

}
