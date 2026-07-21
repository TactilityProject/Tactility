// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

struct RgbDisplayConfig {
    uint16_t horizontal_resolution;
    uint16_t vertical_resolution;

    uint32_t pixel_clock_hz;
    uint32_t hsync_pulse_width;
    uint32_t hsync_back_porch;
    uint32_t hsync_front_porch;
    uint32_t vsync_pulse_width;
    uint32_t vsync_back_porch;
    uint32_t vsync_front_porch;
    bool hsync_idle_low;
    bool vsync_idle_low;
    bool de_idle_high;
    bool pclk_active_neg;
    bool pclk_idle_high;

    // Number of parallel data lines actually wired (8 or 16). Only the first data_width
    // pin-dataN fields below are used; the rest are ignored.
    uint8_t data_width;
    // Frame buffer color depth in bpp. 0 defaults to data_width.
    uint8_t bits_per_pixel;
    // Number of screen-sized frame buffers the driver allocates (0 or 1 = single-buffered).
    uint8_t num_fbs;
    // Non-zero enables the DRAM bounce-buffer DMA path, sized in pixels.
    uint32_t bounce_buffer_size_px;
    uint32_t sram_trans_align;
    uint32_t psram_trans_align;

    struct GpioPinSpec pin_hsync;
    struct GpioPinSpec pin_vsync;
    struct GpioPinSpec pin_de;
    struct GpioPinSpec pin_pclk;
    // Optional display-enable control pin, GPIO_PIN_SPEC_NONE if unused.
    struct GpioPinSpec pin_disp;
    // Optional hardware reset pin for the panel's own driver IC, pulsed once at start() before
    // the RGB peripheral is brought up. GPIO_PIN_SPEC_NONE if the panel has none (esp_lcd_rgb_panel
    // itself has no reset concept - some RGB panels still have a separate reset line on their
    // internal driver IC even though pixel data is fed over the RGB bus).
    struct GpioPinSpec pin_reset;
    bool reset_active_high;

    struct GpioPinSpec pin_data0;
    struct GpioPinSpec pin_data1;
    struct GpioPinSpec pin_data2;
    struct GpioPinSpec pin_data3;
    struct GpioPinSpec pin_data4;
    struct GpioPinSpec pin_data5;
    struct GpioPinSpec pin_data6;
    struct GpioPinSpec pin_data7;
    struct GpioPinSpec pin_data8;
    struct GpioPinSpec pin_data9;
    struct GpioPinSpec pin_data10;
    struct GpioPinSpec pin_data11;
    struct GpioPinSpec pin_data12;
    struct GpioPinSpec pin_data13;
    struct GpioPinSpec pin_data14;
    struct GpioPinSpec pin_data15;

    bool disp_active_low;
    bool refresh_on_demand;
    bool fb_in_psram;
    bool double_fb;
    bool no_fb;
    bool bb_invalidate_cache;

    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    bool invert_color;

    // Optional reference to this display's backlight device, NULL if none.
    struct Device* backlight;
};

#ifdef __cplusplus
}
#endif
