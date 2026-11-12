// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/drivers/trackball.h>
#include <tactility/error.h>

#define TRACKBALL_DRIVER_API(driver) ((struct TrackballApi*)driver->api)

extern "C" {

error_t trackball_read_delta(Device* device, int32_t* out_dx, int32_t* out_dy) {
    const auto* driver = device_get_driver(device);
    return TRACKBALL_DRIVER_API(driver)->read_delta(device, out_dx, out_dy);
}

error_t trackball_get_button_pressed(Device* device, bool* out_pressed) {
    const auto* driver = device_get_driver(device);
    return TRACKBALL_DRIVER_API(driver)->get_button_pressed(device, out_pressed);
}

error_t trackball_set_settings(Device* device, const TrackballSettings* settings) {
    const auto* driver = device_get_driver(device);
    return TRACKBALL_DRIVER_API(driver)->set_settings(device, settings);
}

error_t trackball_get_settings(Device* device, TrackballSettings* out_settings) {
    const auto* driver = device_get_driver(device);
    return TRACKBALL_DRIVER_API(driver)->get_settings(device, out_settings);
}

TrackballSettings trackball_settings_get_default() {
    return TrackballSettings {
        .mode = TRACKBALL_MODE_KEYBOARD,
        .enabled = true,
        .key_sensitivity = 1,
        .pointer_sensitivity = 10,
    };
}

const DeviceType TRACKBALL_TYPE {
    .name = "trackball"
};

}
