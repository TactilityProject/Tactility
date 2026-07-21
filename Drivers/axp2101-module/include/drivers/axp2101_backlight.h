// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#include <drivers/axp2101.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Devicetree configuration for a backlight driven by a switchable/adjustable AXP2101 LDO.
 */
struct Axp2101BacklightConfig {
    /** The AXP2101 LDO powering the backlight */
    enum Axp2101Ldo ldo;
    /** LDO voltage at brightness level 0. Brightness level 0 always disables the LDO outright,
     * rather than actually driving it to this voltage - see set_brightness() in BacklightApi. */
    uint16_t min_millivolt;
    /** LDO voltage at the maximum brightness level (255) */
    uint16_t max_millivolt;
    /** Default brightness level, applied by set_brightness_default() */
    uint8_t brightness_default;
};

#ifdef __cplusplus
}
#endif
