// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include <tactility/drivers/gpio.h>
#include <tactility/drivers/rgb_led.h>

/**
 * @brief Devicetree configuration for the GPIO-driven RGB LED.
 */
struct RgbLedGpioConfig {
    /** Red channel output pin */
    struct GpioPinSpec pin_red;
    /** Green channel output pin */
    struct GpioPinSpec pin_green;
    /** Blue channel output pin */
    struct GpioPinSpec pin_blue;
    /** Whether the LED is turned on by default */
    bool enabled;
    /** Color applied when the LED is turned on by default */
    struct RgbLedColor default_color;
};

#ifdef __cplusplus
}
#endif
