// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/display.h>
#include <tactility/device.h>

#define DISPLAY_DRIVER_API(driver) ((struct DisplayApi*)driver->api)

extern "C" {

bool display_has_capability(struct Device* device, uint32_t capability) {
    const auto* driver = device_get_driver(device);
    const auto* api = DISPLAY_DRIVER_API(driver);
    if (api->has_capability != nullptr) {
        return api->has_capability(device, capability);
    }
    return (api->capabilities & capability) != 0;
}

error_t display_reset(Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->reset(device);
}

error_t display_init(Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->init(device);
}

error_t display_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->draw_bitmap(device, x_start, y_start, x_end, y_end, color_data);
}

error_t display_mirror(Device* device, bool x_axis, bool y_axis) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->mirror(device, x_axis, y_axis);
}

error_t display_swap_xy(Device* device, bool swap_axes) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->swap_xy(device, swap_axes);
}

bool display_get_swap_xy(Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->get_swap_xy(device);
}

bool display_get_mirror_x(Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->get_mirror_x(device);
}

bool display_get_mirror_y(Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->get_mirror_y(device);
}

error_t display_set_gap(Device* device, int32_t x_gap, int32_t y_gap) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->set_gap(device, x_gap, y_gap);
}

error_t display_invert_color(Device* device, bool invert_color_data) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->invert_color(device, invert_color_data);
}

error_t display_disp_on_off(Device* device, bool on_off) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->disp_on_off(device, on_off);
}

error_t display_disp_sleep(Device* device, bool sleep) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->disp_sleep(device, sleep);
}

enum DisplayColorFormat display_get_color_format(Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->get_color_format(device);
}

uint16_t display_get_resolution_x(Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->get_resolution_x(device);
}

uint16_t display_get_resolution_y(Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->get_resolution_y(device);
}

void display_get_frame_buffer(Device* device, uint8_t index, void** out_buffer) {
    const auto* driver = device_get_driver(device);
    DISPLAY_DRIVER_API(driver)->get_frame_buffer(device, index, out_buffer);
}

uint8_t display_get_frame_buffer_count(Device* device) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->get_frame_buffer_count(device);
}

error_t display_get_backlight(Device* device, Device** backlight) {
    const auto* driver = device_get_driver(device);
    return DISPLAY_DRIVER_API(driver)->get_backlight(device, backlight);
}

const struct DeviceType DISPLAY_TYPE {
    .name = "display"
};

}
