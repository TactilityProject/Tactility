// SPDX-License-Identifier: Apache-2.0
#include <drivers/tdeck_keyboard_backlight.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#include <cstdlib>

#define TAG "TdeckKeyboardBacklight"
#define GET_CONFIG(device) (static_cast<const TdeckKeyboardBacklightConfig*>((device)->config))

static constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(100);
static constexpr uint8_t CMD_BRIGHTNESS = 0x01;
static constexpr uint8_t CMD_DEFAULT_BRIGHTNESS = 0x02;

struct TdeckKeyboardBacklightInternal {
    // The controller is write-only for brightness, so the current value is cached here.
    uint8_t current_brightness;
};

// region Driver lifecycle

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    auto address = GET_CONFIG(device)->address;
    if (i2c_controller_has_device_at_address(parent, address, I2C_TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "No device found on I2C bus at address 0x%02X", address);
        return ERROR_RESOURCE;
    }

    auto* internal = static_cast<TdeckKeyboardBacklightInternal*>(malloc(sizeof(TdeckKeyboardBacklightInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->current_brightness = 0;
    device_set_driver_data(device, internal);

    auto brightness_default = GET_CONFIG(device)->brightness_default;

    // Configures the keyboard controller's own persisted default, used by its onboard ALT+B toggle.
    if (i2c_controller_write_register(parent, address, CMD_DEFAULT_BRIGHTNESS, &brightness_default, 1, I2C_TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to set default brightness");
    }

    if (i2c_controller_write_register(parent, address, CMD_BRIGHTNESS, &brightness_default, 1, I2C_TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to set initial brightness");
    } else {
        internal->current_brightness = brightness_default;
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<TdeckKeyboardBacklightInternal*>(device_get_driver_data(device));
    free(internal);
    return ERROR_NONE;
}

// endregion

// region BacklightApi

static error_t tdeck_keyboard_backlight_set_brightness(Device* device, uint8_t brightness) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    auto* internal = static_cast<TdeckKeyboardBacklightInternal*>(device_get_driver_data(device));

    if (i2c_controller_write_register(parent, address, CMD_BRIGHTNESS, &brightness, 1, I2C_TIMEOUT) != ERROR_NONE) {
        return ERROR_RESOURCE;
    }

    internal->current_brightness = brightness;
    return ERROR_NONE;
}

static error_t tdeck_keyboard_backlight_set_brightness_default(Device* device) {
    return tdeck_keyboard_backlight_set_brightness(device, GET_CONFIG(device)->brightness_default);
}

static error_t tdeck_keyboard_backlight_get_brightness(Device* device, uint8_t* out_brightness) {
    auto* internal = static_cast<TdeckKeyboardBacklightInternal*>(device_get_driver_data(device));
    *out_brightness = internal->current_brightness;
    return ERROR_NONE;
}

static uint8_t tdeck_keyboard_backlight_get_min_brightness(Device*) {
    return 0;
}

static uint8_t tdeck_keyboard_backlight_get_max_brightness(Device*) {
    return 255;
}

// endregion

static const BacklightApi tdeck_keyboard_backlight_api = {
    .set_brightness = tdeck_keyboard_backlight_set_brightness,
    .set_brightness_default = tdeck_keyboard_backlight_set_brightness_default,
    .get_brightness = tdeck_keyboard_backlight_get_brightness,
    .get_min_brightness = tdeck_keyboard_backlight_get_min_brightness,
    .get_max_brightness = tdeck_keyboard_backlight_get_max_brightness,
};

extern struct Module lilygo_tdeck_module;

Driver tdeck_keyboard_backlight_driver = {
    .name = "tdeck_keyboard_backlight",
    .compatible = (const char*[]) { "lilygo,tdeck-keyboard-backlight", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &tdeck_keyboard_backlight_api,
    .device_type = &BACKLIGHT_TYPE,
    .owner = &lilygo_tdeck_module,
    .internal = nullptr
};
