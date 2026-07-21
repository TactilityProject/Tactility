// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/drivers/gpio.h>

struct Gt911Config {
    // Devicetree address hint. The driver auto-probes both known GT911 addresses
    // (0x5D primary, 0x14 backup) regardless, since the effective address depends on
    // the controller's INT pin strapping level at power-up, not a fixed board wiring choice.
    uint8_t address;
    uint16_t x_max;
    uint16_t y_max;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    struct GpioPinSpec pin_reset;
    struct GpioPinSpec pin_interrupt;
};

#ifdef __cplusplus
}
#endif
