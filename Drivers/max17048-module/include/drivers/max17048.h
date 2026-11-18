// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct Max17048Config {
    /** The I2C address of the chip, which is 0x36 */
    uint8_t address;
};

#ifdef __cplusplus
}
#endif
