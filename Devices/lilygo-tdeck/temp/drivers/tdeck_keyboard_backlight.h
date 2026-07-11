// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct TdeckKeyboardBacklightConfig {
    uint8_t address;
    uint8_t brightness_default;
};

#ifdef __cplusplus
}
#endif
