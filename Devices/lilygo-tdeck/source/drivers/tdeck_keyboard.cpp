// SPDX-License-Identifier: Apache-2.0
#include <drivers/tdeck_keyboard.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/log.h>

#include <cstdlib>

#define TAG "TdeckKeyboard"
#define GET_CONFIG(device) (static_cast<const TdeckKeyboardConfig*>((device)->config))

static constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(100);

struct TdeckKeyboardInternal {
    // The T-Deck keyboard controller only ever reports the currently pressed key (0x00 = none) and
    // never a release event, so release events are synthesized by comparing against this.
    uint8_t last_key;
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

    auto* internal = static_cast<TdeckKeyboardInternal*>(malloc(sizeof(TdeckKeyboardInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->last_key = 0;

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<TdeckKeyboardInternal*>(device_get_driver_data(device));
    free(internal);
    return ERROR_NONE;
}

// endregion

// region KeyboardApi

static error_t tdeck_keyboard_read_key(Device* device, KeyboardKeyData* data) {
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    auto* internal = static_cast<TdeckKeyboardInternal*>(device_get_driver_data(device));

    uint8_t read_buffer = 0;
    if (i2c_controller_read(parent, address, &read_buffer, 1, I2C_TIMEOUT) != ERROR_NONE) {
        return ERROR_RESOURCE;
    }

    // This controller never buffers more than one key event.
    data->continue_reading = false;

    if (read_buffer == 0 && internal->last_key != 0) {
        data->key = internal->last_key;
        data->pressed = false;
    } else if (read_buffer != 0) {
        data->key = read_buffer;
        data->pressed = true;
    } else {
        data->key = 0;
        data->pressed = false;
    }

    internal->last_key = read_buffer;
    return ERROR_NONE;
}

// endregion

static const KeyboardApi tdeck_keyboard_api = {
    .read_key = tdeck_keyboard_read_key,
};

extern Module lilygo_tdeck_module;

Driver tdeck_keyboard_driver = {
    .name = "tdeck_keyboard",
    .compatible = (const char*[]) { "lilygo,tdeck-keyboard", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &tdeck_keyboard_api,
    .device_type = &KEYBOARD_TYPE,
    .owner = &lilygo_tdeck_module,
    .internal = nullptr
};
