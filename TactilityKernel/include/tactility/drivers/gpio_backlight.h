// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include <tactility/drivers/gpio.h>

/**
 * @brief Devicetree configuration for a GPIO-driven on/off backlight.
 */
struct GpioBacklightConfig {
    /** Backlight enable output pin */
    struct GpioPinSpec pin;
    /** Whether the backlight is turned on by set_brightness_default() */
    bool enabled;
};

#ifdef __cplusplus
}
#endif
