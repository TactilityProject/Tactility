// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/drivers/gpio.h>

/**
 * The struct field order matches the binding's property order and must stay in sync with it:
 * the devicetree compiler emits struct initializers in binding order, not declaration order.
 */
struct Ssd1677Config {
    struct GpioPinSpec pin_dc;
    struct GpioPinSpec pin_reset;
    struct GpioPinSpec pin_busy;
    struct GpioPinSpec pin_cs;
    /** SPI clock frequency in Hz */
    int clock_speed_hz;
    /** Display rotation, 0 = 0 degrees, 1 = 90, 2 = 180, 3 = 270 degrees counter-clockwise */
    uint8_t rotation;
};

#ifdef __cplusplus
}
#endif
