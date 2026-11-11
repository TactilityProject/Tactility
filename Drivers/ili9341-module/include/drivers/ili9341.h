// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

struct Ili9341Config {
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
    uint32_t pixel_clock_hz;
    uint8_t transaction_queue_depth;
    // Gamma curve preset index [0,3], sent via the MIPI DCS GAMSET (0x26) command at bring-up.
    uint8_t gamma_curve;
    struct GpioPinSpec pin_dc;
    struct GpioPinSpec pin_reset;
    bool reset_active_high;
    // Custom vendor init sequence, flattened as bytes: a run of
    // [cmd, data_len, delay_ms, data_len bytes of data...] entries. NULL/0 falls back to the
    // ILI9341 component's own built-in default sequence (see vendor_specific_init_default in
    // esp_lcd_ili9341.c).
    const uint8_t* init_sequence;
    uint32_t init_sequence_length;
    // Optional reference to this display's backlight device, NULL if none.
    struct Device* backlight;
};

#ifdef __cplusplus
}
#endif
