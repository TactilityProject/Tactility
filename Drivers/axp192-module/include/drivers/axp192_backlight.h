// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#include <drivers/axp192.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Devicetree configuration for a backlight driven by a switchable/adjustable AXP192 power rail.
 */
struct Axp192BacklightConfig {
    /** The AXP192 rail powering the backlight */
    enum Axp192Rail rail;
    /** Rail voltage at brightness level 0. Brightness level 0 always disables the rail outright,
     * rather than actually driving it to this voltage - see set_brightness() in BacklightApi. */
    uint16_t min_millivolt;
    /** Rail voltage at the maximum brightness level (255) */
    uint16_t max_millivolt;
    /** Default brightness level, applied by set_brightness_default() */
    uint8_t brightness_default;
};

#ifdef __cplusplus
}
#endif
