// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct Bq25896Config {
    // Devicetree address hint (0x6B).
    uint8_t address;
};

#ifdef __cplusplus
}
#endif
