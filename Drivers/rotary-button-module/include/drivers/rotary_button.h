// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/drivers/gpio.h>

struct RotaryButtonConfig {
    struct GpioPinSpec pin_up;
    struct GpioPinSpec pin_down;
    struct GpioPinSpec pin_press;
    uint32_t debounce_ms;
};

#ifdef __cplusplus
}
#endif
