// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/backlight.h>
#include <tactility/device.h>

#define BACKLIGHT_DRIVER_API(driver) ((struct BacklightApi*)driver->api)

extern "C" {

error_t backlight_set_brightness(Device* device, uint8_t brightness) {
    const auto* driver = device_get_driver(device);
    return BACKLIGHT_DRIVER_API(driver)->set_brightness(device, brightness);
}

error_t backlight_set_brightness_default(Device* device) {
    const auto* driver = device_get_driver(device);
    return BACKLIGHT_DRIVER_API(driver)->set_brightness_default(device);
}

error_t backlight_get_brightness(Device* device, uint8_t* out_brightness) {
    const auto* driver = device_get_driver(device);
    return BACKLIGHT_DRIVER_API(driver)->get_brightness(device, out_brightness);
}

uint8_t backlight_get_min_brightness(Device* device) {
    const auto* driver = device_get_driver(device);
    return BACKLIGHT_DRIVER_API(driver)->get_min_brightness(device);
}

uint8_t backlight_get_max_brightness(Device* device) {
    const auto* driver = device_get_driver(device);
    return BACKLIGHT_DRIVER_API(driver)->get_max_brightness(device);
}

const DeviceType BACKLIGHT_TYPE {
    .name = "backlight"
};

}
