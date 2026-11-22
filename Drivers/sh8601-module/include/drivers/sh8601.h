// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/gpio.h>

struct Sh8601Config {
    // From display.yaml: a DisplayShapeType value and the corner radius for rounded displays
    uint8_t shape;
    uint16_t corner_radius;
    uint16_t horizontal_resolution;
    uint16_t vertical_resolution;
    int32_t gap_x;
    int32_t gap_y;
    bool invert_color;
    uint32_t pixel_clock_hz;
    uint8_t transaction_queue_depth;
    struct GpioPinSpec pin_reset;
};

#ifdef __cplusplus
}
#endif
