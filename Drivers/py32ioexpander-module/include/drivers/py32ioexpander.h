// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Py32IoExpanderConfig {
    uint8_t address;
};

// ---------------------------------------------------------------------------
// GPIO (16 pins, index 0–15)
// ---------------------------------------------------------------------------
error_t py32_gpio_set_output(struct Device* device, uint8_t pin, bool value);
error_t py32_gpio_get_input(struct Device* device, uint8_t pin, bool* value);

// ---------------------------------------------------------------------------
// NeoPixel LED ring (up to 32 WS2812C LEDs)
// ---------------------------------------------------------------------------
error_t py32_led_set_count(struct Device* device, uint8_t count);
error_t py32_led_set_color(struct Device* device, uint8_t index, uint8_t r, uint8_t g, uint8_t b);
error_t py32_led_refresh(struct Device* device);
error_t py32_led_disable(struct Device* device);

#ifdef __cplusplus
}
#endif
