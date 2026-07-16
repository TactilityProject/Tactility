// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/rgb_led.h>

/**
 * @brief Devicetree configuration for the PWM-driven RGB LED.
 */
struct RgbLedPwmConfig {
    /** PWM device driving the red channel */
    struct Device* pwm_red;
    /** PWM device driving the green channel */
    struct Device* pwm_green;
    /** PWM device driving the blue channel */
    struct Device* pwm_blue;
    /** Whether the LED is turned on by default */
    bool enabled;
    /** Color applied when the LED is turned on by default */
    struct RgbLedColor default_color;
};

#ifdef __cplusplus
}
#endif
