// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/drivers/gpio.h>

struct Cst328Config {
    // Devicetree address hint (0x1A on the LilyGO T-Deck Pro).
    uint8_t address;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    struct GpioPinSpec pin_reset;
    bool reset_active_high;
};

#ifdef __cplusplus
}
#endif
