// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

struct St7305Config {
    uint16_t horizontal_resolution;
    uint16_t vertical_resolution;
    uint8_t column_offset;
    bool mirror_x;
    bool mirror_y;
    bool invert_color;
    uint32_t pixel_clock_hz;
    struct GpioPinSpec pin_dc;
    struct GpioPinSpec pin_reset;
};

#ifdef __cplusplus
}
#endif
