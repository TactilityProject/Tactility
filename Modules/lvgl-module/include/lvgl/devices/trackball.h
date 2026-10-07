// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#include <tactility/device.h>
#include <tactility/error.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Trackball operating mode.
 */
enum LvglTrackballMode {
    /** Navigation with arrow keys, and the button as the enter key. No cursor is shown. */
    LVGL_TRACKBALL_MODE_KEYS,
    /** Mouse cursor via point.x/point.y. A cursor is shown if an image was set via
     *  lvgl_trackball_set_cursor_image(). */
    LVGL_TRACKBALL_MODE_POINTER
};

/**
 * @brief Runtime-adjustable trackball behavior. See lvgl_trackball_set_settings()/
 * lvgl_trackball_get_settings().
 */
struct LvglTrackballSettings {
    enum LvglTrackballMode mode;
    /** When false, movement and button presses are drained from the device and discarded:
     *  the indev reports no movement and stays released. */
    bool enabled;
    /** Keys mode: arrow key presses per raw trackball tick. Must be >= 1. */
    uint8_t key_sensitivity;
    /** Pointer mode: pixels per raw trackball tick. Must be >= 1. */
    uint8_t pointer_sensitivity;
};

/**
 * @brief Creates an lv_indev_t bound to the given TRACKBALL_TYPE device and registers a read
 * callback that polls the device through its TrackballApi.
 *
 * The indev starts in LVGL_TRACKBALL_MODE_KEYS, enabled, with default sensitivities (see
 * lvgl_trackball_settings_get_default()). Call lvgl_trackball_set_settings() to change this,
 * e.g. to switch to pointer mode - each device added this way can independently run with or
 * without pointer support.
 *
 * @warning Caller must hold the LVGL lock (see lvgl_lock() in lvgl_module.h) — call this from
 * LvglModuleConfig.on_start, or after calling lvgl_lock() explicitly.
 *
 * @param[in] device a device of type TRACKBALL_TYPE
 * @param[in] display the display this indev should be associated with, or NULL to leave it unset
 * @param[out] out_indev the created indev, valid only when ERROR_NONE is returned
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT if device or out_indev is NULL, or device is not of type TRACKBALL_TYPE
 * @retval ERROR_OUT_OF_MEMORY if allocation failed
 */
error_t lvgl_trackball_add(struct Device* device, lv_display_t* display, lv_indev_t** out_indev);

/**
 * @brief Removes an indev previously created with lvgl_trackball_add(), including its cursor
 * object (if one was showing).
 * @warning Caller must hold the LVGL lock.
 */
void lvgl_trackball_remove(lv_indev_t* indev);

/**
 * @return the default (all fields explicitly set) LvglTrackballSettings: keys mode, enabled,
 * key_sensitivity 1, pointer_sensitivity 10.
 */
struct LvglTrackballSettings lvgl_trackball_settings_get_default(void);

/**
 * @brief Applies settings to an indev previously created with lvgl_trackball_add(). Switching
 * `mode` shows/hides the cursor (pointer mode) and re-centers it on the indev's display.
 * @warning Caller must hold the LVGL lock.
 * @param[in] indev an indev previously created by lvgl_trackball_add()
 * @param[in] settings the settings to apply
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT if indev or settings is NULL, or either sensitivity is 0
 */
error_t lvgl_trackball_set_settings(lv_indev_t* indev, const struct LvglTrackballSettings* settings);

/**
 * @brief Retrieves the settings currently active on indev.
 * @warning Caller must hold the LVGL lock.
 * @return true when indev is a trackball indev (out_settings is filled), false otherwise
 */
bool lvgl_trackball_get_settings(lv_indev_t* indev, struct LvglTrackballSettings* out_settings);

/**
 * @brief Sets the image shown as the mouse cursor while indev is in LVGL_TRACKBALL_MODE_POINTER.
 * lvgl-module has no bundled cursor asset (it doesn't depend on any particular asset layout), so
 * pointer mode has no visible cursor until a caller supplies one here; the indev still moves and
 * reports point.x/point.y either way. Pass NULL to remove the cursor.
 * @warning Caller must hold the LVGL lock.
 * @param[in] indev an indev previously created by lvgl_trackball_add()
 * @param[in] image_src an LVGL image source (as accepted by lv_image_set_src()), or NULL
 */
void lvgl_trackball_set_cursor_image(lv_indev_t* indev, const void* image_src);

#ifdef __cplusplus
}
#endif
