// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/drivers/gpio.h>

struct Xpt2046SoftSpiConfig {
    struct GpioPinSpec pin_mosi;
    struct GpioPinSpec pin_miso;
    struct GpioPinSpec pin_sck;
    struct GpioPinSpec pin_cs;
    uint16_t x_max;
    uint16_t y_max;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
};

#ifdef __cplusplus
}
#endif
