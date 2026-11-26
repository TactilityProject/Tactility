// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/drivers/gpio.h>

/**
 * The struct field order matches the binding's property order and must stay in sync with it:
 * the devicetree compiler emits struct initializers in binding order, not declaration order.
 */
struct Uc8253Config {
    struct GpioPinSpec pin_dc;
    struct GpioPinSpec pin_reset;
    struct GpioPinSpec pin_busy;
    struct GpioPinSpec pin_cs;
    /** SPI clock frequency in Hz */
    int clock_speed_hz;
    /** Panel name, see the binding's panel-type */
    const char* panel_type;
    // Optional reference to this display's backlight device, NULL if none.
    struct Device* backlight;
};

#ifdef __cplusplus
}
#endif
