// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/device.h>
#include <tactility/drivers/backlight.h>

/**
 * @brief Devicetree configuration for a PWM-driven backlight.
 */
struct PwmBacklightConfig {
    /** The PWM device driving the backlight */
    struct Device* pwm;
    /** Inclusive [min,max] brightness range. The minimum value turns the backlight off. */
    struct BrightnessLevelRange brightness_range;
    /** Default brightness level, applied by set_brightness_default() */
    uint8_t brightness_default;
};

#ifdef __cplusplus
}
#endif
