// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/display.h>
#include <tactility/device.h>

#define DISPLAY_DRIVER_API(driver) ((struct DisplayApi*)driver->api)

extern "C" {

error_t display_reset(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->reset(device);
}

error_t display_init(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->init(device);
}

error_t display_draw_bitmap(struct Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->draw_bitmap(device, x_start, y_start, x_end, y_end, color_data);
}

error_t display_mirror(struct Device* device, bool x_axis, bool y_axis) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->mirror(device, x_axis, y_axis);
}

error_t display_swap_xy(struct Device* device, bool swap_axes) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->swap_xy(device, swap_axes);
}

error_t display_set_gap(struct Device* device, int32_t x_gap, int32_t y_gap) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->set_gap(device, x_gap, y_gap);
}

error_t display_invert_color(struct Device* device, bool invert_color_data) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->invert_color(device, invert_color_data);
}

error_t display_disp_on_off(struct Device* device, bool on_off) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->disp_on_off(device, on_off);
}

error_t display_disp_sleep(struct Device* device, bool sleep) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->disp_sleep(device, sleep);
}

enum DisplayColorFormat display_get_color_format(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->get_color_format(device);
}

uint16_t display_get_resolution_x(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->get_resolution_x(device);
}

uint16_t display_get_resolution_y(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->get_resolution_y(device);
}

void display_get_frame_buffer(struct Device* device, uint8_t index, void** out_buffer) {
    const auto* driver = device_get_driver(device);
    DISPLAY_DRIVER_API(driver)->get_frame_buffer(device, index, out_buffer);
}

uint8_t display_get_frame_buffer_count(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->get_frame_buffer_count(device);
}

const struct DeviceType DISPLAY_TYPE {
    .name = "display"
};

}
