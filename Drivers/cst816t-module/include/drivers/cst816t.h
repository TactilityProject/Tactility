// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/drivers/gpio.h>

struct Cst816tConfig {
    uint8_t address;
    uint16_t x_max;
    uint16_t y_max;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    struct GpioPinSpec pin_reset;
    struct GpioPinSpec pin_interrupt;
    bool reset_active_high;
    bool interrupt_active_high;
};

#ifdef __cplusplus
}
#endif
