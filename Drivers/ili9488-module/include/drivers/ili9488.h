// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

struct Ili9488Config {
    uint16_t horizontal_resolution;
    uint16_t vertical_resolution;
    int32_t gap_x;
    int32_t gap_y;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    bool invert_color;
    bool bgr_order;
    uint32_t bits_per_pixel;
    // Size (in pixels) of the internal buffer the panel driver uses to convert
    // 16bpp draw data to the panel's native 18bpp format.
    uint32_t color_conversion_buffer_size;
    uint32_t pixel_clock_hz;
    uint8_t transaction_queue_depth;
    struct GpioPinSpec pin_dc;
    struct GpioPinSpec pin_reset;
    // Optional reference to this display's backlight device, NULL if none.
    struct Device* backlight;
};

#ifdef __cplusplus
}
#endif
