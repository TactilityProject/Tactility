// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/keyboard.h>
#include <tactility/error.h>
#include <tactility/device.h>

#define KEYBOARD_DRIVER_API(driver) ((struct KeyboardApi*)driver->api)

extern "C" {

error_t keyboard_read_key(Device* device, KeyboardKeyData* data) {
    const auto* driver = device_get_driver(device);
    return KEYBOARD_DRIVER_API(driver)->read_key(device, data);
}

error_t keyboard_get_backlight(Device* device, Device** backlight_device) {
    const auto* driver = device_get_driver(device);

    if (KEYBOARD_DRIVER_API(driver)->get_backlight == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    return KEYBOARD_DRIVER_API(driver)->get_backlight(device, backlight_device);
}

const DeviceType KEYBOARD_TYPE {
    .name = "keyboard"
};

}
