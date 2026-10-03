// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/led_strip.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/log.h>

#define TAG "LedStrip"

#define LED_STRIP_DRIVER_API(driver) ((struct LedStripApi*)driver->api)

extern "C" {

error_t led_strip_set_single_led_color(struct Device* device, size_t index, const struct LedRgb color) {
    const auto* driver = device_get_driver(device);
    return LED_STRIP_DRIVER_API(driver)->set_single_led_color(device, index, color);
}

error_t led_strip_set_single_led_color_and_brightness(struct Device* device, size_t index, const struct LedRgb color, uint8_t brightness) {
    const auto* driver = device_get_driver(device);
    return LED_STRIP_DRIVER_API(driver)->set_single_led_color_and_brightness(device, index, color, brightness);
}

error_t led_strip_set_led_range_color(struct Device* device, size_t first, size_t count, const struct LedRgb* color) {
    const auto* driver = device_get_driver(device);
    return LED_STRIP_DRIVER_API(driver)->set_led_range_color(device, first, count, color);
}

error_t led_strip_set_led_range_color_and_brightness(struct Device* device, size_t first, size_t count, const struct LedRgb* color, uint8_t brightness) {
    const auto* driver = device_get_driver(device);
    return LED_STRIP_DRIVER_API(driver)->set_led_range_color_and_brightness(device, first, count, color, brightness);
}

error_t led_strip_fill_led_range_color(struct Device* device, size_t first, size_t count, const struct LedRgb color) {
    const auto* driver = device_get_driver(device);
    return LED_STRIP_DRIVER_API(driver)->fill_led_range_color(device, first, count, color);
}

error_t led_strip_fill_led_range_color_and_brightness(struct Device* device, size_t first, size_t count, const struct LedRgb color, uint8_t brightness) {
    const auto* driver = device_get_driver(device);
    return LED_STRIP_DRIVER_API(driver)->fill_led_range_color_and_brightness(device, first, count, color, brightness);
}

error_t led_strip_set_brightness(struct Device* device, uint8_t brightness) {
    const auto* driver = device_get_driver(device);
    return LED_STRIP_DRIVER_API(driver)->set_brightness(device, brightness);
}

error_t led_strip_show(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return LED_STRIP_DRIVER_API(driver)->show(device);
}

error_t led_strip_clear(struct Device* device) {
    const auto* driver = device_get_driver(device);
    return LED_STRIP_DRIVER_API(driver)->clear(device);
}

error_t led_strip_get_length(struct Device* device, uint16_t* out_length) {
    const auto* driver = device_get_driver(device);
    return LED_STRIP_DRIVER_API(driver)->get_length(device, out_length);
}

const struct DeviceType LED_STRIP_TYPE {
    .name = "led_strip"
};

}
