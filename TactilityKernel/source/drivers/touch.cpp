// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/touch.h>
#include <tactility/device.h>

#define TOUCH_DRIVER_API(driver) ((struct TouchApi*)driver->api)

extern "C" {

error_t touch_enter_sleep(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return TOUCH_DRIVER_API(driver)->enter_sleep(device);
}

error_t touch_exit_sleep(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return TOUCH_DRIVER_API(driver)->exit_sleep(device);
}

error_t touch_read_data(struct Device* device, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return TOUCH_DRIVER_API(driver)->read_data(device, timeout);
}

bool touch_get_touched_points(struct Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    const auto* driver = device_get_driver(device);
    return TOUCH_DRIVER_API(driver)->get_touched_points(device, x, y, strength, point_count, max_point_count);
}

error_t touch_set_swap_xy(struct Device* device, bool swap) {
    const auto* driver = device_get_driver(device);
    return TOUCH_DRIVER_API(driver)->set_swap_xy(device, swap);
}

error_t touch_get_swap_xy(struct Device* device, bool* swap) {
    const auto* driver = device_get_driver(device);
    return TOUCH_DRIVER_API(driver)->get_swap_xy(device, swap);
}

error_t touch_set_mirror_x(struct Device* device, bool mirror) {
    const auto* driver = device_get_driver(device);
    return TOUCH_DRIVER_API(driver)->set_mirror_x(device, mirror);
}

error_t touch_get_mirror_x(struct Device* device, bool* mirror) {
    const auto* driver = device_get_driver(device);
    return TOUCH_DRIVER_API(driver)->get_mirror_x(device, mirror);
}

error_t touch_set_mirror_y(struct Device* device, bool mirror) {
    const auto* driver = device_get_driver(device);
    return TOUCH_DRIVER_API(driver)->set_mirror_y(device, mirror);
}

error_t touch_get_mirror_y(struct Device* device, bool* mirror) {
    const auto* driver = device_get_driver(device);
    return TOUCH_DRIVER_API(driver)->get_mirror_y(device, mirror);
}

const struct DeviceType TOUCH_TYPE {
    .name = "touch"
};

}
