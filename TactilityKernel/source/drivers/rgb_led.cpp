// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/rgb_led.h>
#include <tactility/device.h>

#define RGB_LED_DRIVER_API(driver) ((struct RgbLedApi*)driver->api)

extern "C" {

error_t rgb_led_set_color(Device* device, RgbLedColor color) {
    const auto* driver = device_get_driver(device);
    return RGB_LED_DRIVER_API(driver)->set_color(device, color);
}

error_t rgb_led_get_color(Device* device, RgbLedColor* out_color) {
    const auto* driver = device_get_driver(device);
    return RGB_LED_DRIVER_API(driver)->get_color(device, out_color);
}

error_t rgb_led_enable(Device* device) {
    const auto* driver = device_get_driver(device);
    return RGB_LED_DRIVER_API(driver)->enable(device);
}

void rgb_led_disable(Device* device) {
    const auto* driver = device_get_driver(device);
    RGB_LED_DRIVER_API(driver)->disable(device);
}

const DeviceType RGB_LED_TYPE {
    .name = "rgb_led"
};

}
