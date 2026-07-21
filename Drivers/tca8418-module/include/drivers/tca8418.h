// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/drivers/gpio.h>

struct Tca8418Config {
    // Devicetree address hint (0x34 on the LilyGO T-Deck Max).
    uint8_t address;
    uint8_t rows;
    uint8_t columns;
    bool reverse_columns;
    const uint8_t* keymap_lc;
    uint32_t keymap_lc_length;
    const uint8_t* keymap_uc;
    uint32_t keymap_uc_length;
    const uint8_t* keymap_sy;
    uint32_t keymap_sy_length;
    uint8_t shift_row;
    uint8_t shift_col;
    uint8_t sym_row;
    uint8_t sym_col;
    struct GpioPinSpec pin_reset;
};

#ifdef __cplusplus
}
#endif
