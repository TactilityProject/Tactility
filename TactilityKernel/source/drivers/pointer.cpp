// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/pointer.h>
#include <tactility/device.h>

#define POINTER_DRIVER_API(driver) ((struct PointerApi*)driver->api)

extern "C" {

error_t pointer_enter_sleep(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return POINTER_DRIVER_API(driver)->enter_sleep(device);
}

error_t pointer_exit_sleep(Device* device) {
    const auto* driver = device_get_driver(device);
    return POINTER_DRIVER_API(driver)->exit_sleep(device);
}

error_t pointer_read_data(Device* device, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return POINTER_DRIVER_API(driver)->read_data(device, timeout);
}

bool pointer_get_touched_points(Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    const auto* driver = device_get_driver(device);
    return POINTER_DRIVER_API(driver)->get_touched_points(device, x, y, strength, point_count, max_point_count);
}

error_t pointer_set_swap_xy(Device* device, bool swap) {
    const auto* driver = device_get_driver(device);
    return POINTER_DRIVER_API(driver)->set_swap_xy(device, swap);
}

error_t pointer_get_swap_xy(Device* device, bool* swap) {
    const auto* driver = device_get_driver(device);
    return POINTER_DRIVER_API(driver)->get_swap_xy(device, swap);
}

error_t pointer_set_mirror_x(Device* device, bool mirror) {
    const auto* driver = device_get_driver(device);
    return POINTER_DRIVER_API(driver)->set_mirror_x(device, mirror);
}

error_t pointer_get_mirror_x(Device* device, bool* mirror) {
    const auto* driver = device_get_driver(device);
    return POINTER_DRIVER_API(driver)->get_mirror_x(device, mirror);
}

error_t pointer_set_mirror_y(Device* device, bool mirror) {
    const auto* driver = device_get_driver(device);
    return POINTER_DRIVER_API(driver)->set_mirror_y(device, mirror);
}

error_t pointer_get_mirror_y(Device* device, bool* mirror) {
    const auto* driver = device_get_driver(device);
    return POINTER_DRIVER_API(driver)->get_mirror_y(device, mirror);
}

const struct DeviceType POINTER_TYPE {
    .name = "pointer"
};

}
