// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <lvgl.h>

#include <tactility/device.h>
#include <tactility/error.h>

/**
 * @brief Configuration for binding a kernel DisplayApi device to an lv_display_t.
 */
struct LvglDisplayConfig {
    /**
     * Number of horizontal lines per draw buffer. 0 means the full vertical resolution.
     * Ignored when the device exposes its own frame buffer(s) (display_get_frame_buffer_count() > 0).
     */
    uint16_t buffer_height;

    /**
     * Allocates a second draw buffer for double buffering.
     * Ignored when the device exposes its own frame buffer(s).
     */
    bool double_buffer;

    /**
     * Rotate LVGL_rendered content in software instead of calling display_swap_xy()/display_mirror()
     * on the device. Use this for panels whose driver can't rotate in hardware (e.g. RGB/DPI panels).
     * Allocates one extra buffer sized like the primary draw buffer.
     */
    bool sw_rotate;

    /**
     * Endianness of the 2 bytes of each RGB565/BGR565 pixel sent to the panel. False (default)
     * keeps this little-endian CPU's native byte order (no-op). True swaps the 2 bytes of every
     * pixel (big-endian) in the flush callback, via lv_draw_sw_rgb565_swap() - for panels that
     * expect the opposite byte order over the bus. Ignored for color formats other than
     * RGB565/BGR565 (e.g. RGB888, MONOCHROME).
     */
    bool swap_bytes;
};

/**
 * @brief Creates an lv_display_t bound to the given DISPLAY_TYPE device and registers a flush callback
 * that draws through the device's DisplayApi.
 *
 * The device's swap_xy/mirror_x/mirror_y state at the time of this call (via display_get_swap_xy() etc.)
 * is treated as the LV_DISPLAY_ROTATION_0 baseline; LVGL-driven rotation changes are applied relative to it.
 *
 * @warning Caller must hold the LVGL lock (see lvgl_lock() in lvgl_module.h) — call this from
 * LvglModuleConfig.on_start, or after calling lvgl_lock() explicitly.
 *
 * @param[in] device a device of type DISPLAY_TYPE
 * @param[in] config binding configuration
 * @param[out] out_display the created display, valid only when ERROR_NONE is returned
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT if device, config or out_display is NULL, or device is not of type DISPLAY_TYPE
 * @retval ERROR_NOT_SUPPORTED if the device's color format has no LVGL equivalent
 * @retval ERROR_OUT_OF_MEMORY if buffer or lv_display_t allocation failed
 */
error_t lvgl_display_add(struct Device* device, const struct LvglDisplayConfig* config, lv_display_t** out_display);

/**
 * @brief Removes a display previously created with lvgl_display_add(), freeing any buffers it owns.
 * @warning Caller must hold the LVGL lock.
 */
void lvgl_display_remove(lv_display_t* display);

#ifdef __cplusplus
}
#endif
