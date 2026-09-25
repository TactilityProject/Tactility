// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

struct St7789SoftI8080Config {
    uint16_t horizontal_resolution;
    uint16_t vertical_resolution;
    int32_t gap_x;
    int32_t gap_y;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    bool invert_color;
    bool bgr_order;
    struct GpioPinSpec pin_d0;
    struct GpioPinSpec pin_d1;
    struct GpioPinSpec pin_d2;
    struct GpioPinSpec pin_d3;
    struct GpioPinSpec pin_d4;
    struct GpioPinSpec pin_d5;
    struct GpioPinSpec pin_d6;
    struct GpioPinSpec pin_d7;
    struct GpioPinSpec pin_cs;
    struct GpioPinSpec pin_dc;
    struct GpioPinSpec pin_wr;
    struct GpioPinSpec pin_rd;
    struct GpioPinSpec pin_reset;
    struct Device* backlight;
};

#ifdef __cplusplus
}
#endif