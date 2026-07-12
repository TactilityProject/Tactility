// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct M5stackCardputerAdvKeyboardConfig {
    // Devicetree address hint. The TCA8418 has a fixed I2C address (0x34), but the field is
    // still populated from the standard i2c-device "reg" property for consistency with other
    // I2C child device bindings.
    uint8_t address;
};

#ifdef __cplusplus
}
#endif
