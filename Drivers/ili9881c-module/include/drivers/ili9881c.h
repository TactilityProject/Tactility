// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

struct Ili9881cConfig {
    uint16_t horizontal_resolution;
    uint16_t vertical_resolution;
    uint8_t bits_per_pixel;
    bool bgr_order;
    bool invert_color;
    bool mirror_x;
    bool mirror_y;

    // Reset pin for the panel. GPIO_PIN_SPEC_NONE falls back to a software reset sent over the
    // DBI command interface (see esp_lcd_ili9881c's panel_ili9881c_reset()).
    struct GpioPinSpec pin_reset;
    bool reset_active_high;

    // LDO channel powering the MIPI DSI PHY - the PHY has no power of its own until this is
    // acquired, so it must happen before the DSI bus is created. Both fields are int32_t (not
    // uint32_t) to exactly match esp_ldo_channel_config_t's plain `int` fields - assigning a
    // uint32_t into that struct's brace-init would be a narrowing conversion (-Werror=narrowing).
    int32_t ldo_channel;
    int32_t ldo_voltage_mv;

    uint8_t dsi_bus_id;
    // Number of MIPI DSI data lanes. Also passed straight through as the vendor config's
    // mipi_config.lane_num (unlike jd9165, ili9881c_vendor_config_t tracks this separately from
    // the DSI bus config). 0 falls back to the maximum available.
    uint8_t num_data_lanes;
    uint32_t lane_bit_rate_mbps;

    uint32_t dpi_clock_freq_mhz;
    uint32_t hsync_pulse_width;
    uint32_t hsync_back_porch;
    uint32_t hsync_front_porch;
    uint32_t vsync_pulse_width;
    uint32_t vsync_back_porch;
    uint32_t vsync_front_porch;
    // Number of screen-sized frame buffers the driver allocates (0 or 1 = single-buffered).
    uint8_t num_fbs;
    bool use_dma2d;
    bool disable_lp;

    // See the 'allow-tearing' binding property.
    bool allow_tearing;

    // Custom vendor init sequence, flattened as bytes: a run of
    // [cmd, data_len, delay_ms, data_len bytes of data...] entries. NULL/0 falls back to the
    // ILI9881C component's own built-in default sequence (see vendor_specific_init_default in
    // esp_lcd_ili9881c.c) - not guaranteed to match any particular panel's actual bring-up
    // requirements.
    const uint8_t* init_sequence;
    uint32_t init_sequence_length;

    // Optional reference to this display's backlight device, NULL if none.
    struct Device* backlight;
};

#ifdef __cplusplus
}
#endif
