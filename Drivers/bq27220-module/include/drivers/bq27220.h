// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct Bq27220Config {
    // Devicetree address hint (0x55 on the LilyGO T-Deck Max).
    uint8_t address;
};

#ifdef __cplusplus
}
#endif
