// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// True on targets with a PPA (Pixel Processing Accelerator) unit that lvgl_ppa can use to
// hardware-rotate rotated flush tiles instead of lv_draw_sw_rotate(). False (always) on the
// simulator and on ESP32 targets without a PPA (see SOC_PPA_SUPPORTED).
bool lvgl_ppa_is_supported(void);

// True when color_format has a PPA color mode this module supports (RGB565/RGB888 - see
// lvgl_ppa_color_mode()). LV_COLOR_FORMAT_I1 and others fall back to lv_draw_sw_rotate().
bool lvgl_ppa_supports_color_format(lv_color_format_t color_format);

// Lazily creates (on first call) a PPA client and an output buffer at least out_buffer_size_bytes
// large, big enough for the largest tile this display will ever rotate. Returns NULL on failure -
// callers should fall back to lv_draw_sw_rotate() when that happens. Not thread-safe; callers must
// already hold the LVGL lock (true for all flush_cb callers).
void* lvgl_ppa_get_or_create(size_t out_buffer_size_bytes);

void lvgl_ppa_delete(void* ppa_handle);

// Rotates the tightly-packed w x h block at in_buff by rotation (LV_DISPLAY_ROTATION_90/180/270 -
// numerically identical to ppa_srm_rotation_angle_t's CCW convention, see the .c file) into the
// PPA's own output buffer and returns it. in_buff must have no row padding (PPA reads pic_w/pic_h
// in pixels, not a byte stride) - true for every lvgl_display_flush_cb() tile, which LVGL always
// renders compact. Returns NULL on failure - caller should fall back to lv_draw_sw_rotate().
void* lvgl_ppa_rotate(
    void* ppa_handle,
    const uint8_t* in_buff,
    int32_t w,
    int32_t h,
    lv_display_rotation_t rotation,
    lv_color_format_t color_format,
    bool swap_bytes
);

#ifdef __cplusplus
}
#endif
