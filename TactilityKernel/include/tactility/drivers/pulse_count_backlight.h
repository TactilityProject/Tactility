// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/drivers/gpio.h>

/**
 * @brief Devicetree configuration for a backlight driver IC with one-wire pulse dimming (e.g. AW9364).
 */
struct PulseCountBacklightConfig {
    /** The enable pin of the backlight driver IC */
    struct GpioPinSpec pin;
    /** The number of brightness levels. The IC starts at the highest one and every pulse lowers it by one. */
    uint8_t levels;
    /** Default brightness level, applied by set_brightness_default() */
    uint8_t brightness_default;
};

#ifdef __cplusplus
}
#endif
