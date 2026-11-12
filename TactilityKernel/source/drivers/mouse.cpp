// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/drivers/mouse.h>
#include <tactility/error.h>

#define MOUSE_DRIVER_API(driver) ((struct MouseApi*)driver->api)

extern "C" {

error_t mouse_read_delta(Device* device, int32_t* out_dx, int32_t* out_dy) {
    const auto* driver = device_get_driver(device);
    return MOUSE_DRIVER_API(driver)->read_delta(device, out_dx, out_dy);
}

uint8_t mouse_get_button_count(Device* device) {
    const auto* driver = device_get_driver(device);
    return MOUSE_DRIVER_API(driver)->button_count;
}

error_t mouse_get_button_pressed(Device* device, uint8_t index, bool* out_pressed) {
    const auto* driver = device_get_driver(device);
    if (index >= MOUSE_DRIVER_API(driver)->button_count) {
        return ERROR_INVALID_ARGUMENT;
    }
    return MOUSE_DRIVER_API(driver)->get_button_pressed(device, index, out_pressed);
}

const DeviceType MOUSE_TYPE {
    .name = "mouse"
};

}
