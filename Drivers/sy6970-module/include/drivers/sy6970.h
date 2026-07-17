// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct Sy6970Config {
    // Devicetree address hint (0x6A on the LilyGO T-Deck Max; older BQ25896-based boards use 0x6B).
    uint8_t address;
    uint32_t charge_voltage_mv;
    uint32_t charge_current_ma;
};

#ifdef __cplusplus
}
#endif
