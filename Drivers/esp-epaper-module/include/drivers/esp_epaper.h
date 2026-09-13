// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <epaper_config.h>
#include <tactility/drivers/gpio.h>

/**
 * @brief Panel configuration for an esp_epaper-driven e-paper display.
 *
 * The struct field order matches the binding's property order and must stay in
 * sync with it: the devicetree compiler emits struct initializers in binding
 * order, not declaration order.
 */
struct EspEpaperConfig {
    struct GpioPinSpec pin_dc;
    struct GpioPinSpec pin_reset;
    struct GpioPinSpec pin_busy;
    struct GpioPinSpec pin_cs;
    /** SPI clock frequency in Hz */
    int clock_speed_hz;
    /** Panel registry name, e.g. "ssd16xx-290" (see the binding's panel-type) */
    const char* panel_type;
    /** Horizontal resolution override; 0 = the panel's default */
    uint16_t width;
    /** Vertical resolution override; 0 = the panel's default */
    uint16_t height;
    /** epd_update_mode_t used for frame updates */
    epd_update_mode_t update_mode;
    /**
     * Fixed display rotation, 0 = 0 degrees, 1 = 90, 2 = 180, 3 = 270 degrees
     * counter-clockwise (LV_DISPLAY_ROTATION_*). Not changeable at runtime.
     */
    uint8_t rotation;
    /** Mirror the panel horizontally. Fixed at start; the underlying esp_epaper component applies
     * it at epd_init() time with no runtime setter. */
    bool mirror_x;
    /** Mirror the panel vertically. Fixed at start, same as mirror_x. */
    bool mirror_y;
};

#ifdef __cplusplus
}
#endif
