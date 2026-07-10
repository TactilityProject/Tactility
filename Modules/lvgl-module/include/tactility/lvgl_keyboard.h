// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl.h>

#include <tactility/device.h>
#include <tactility/error.h>

/**
 * @brief Creates an lv_indev_t bound to the given KEYBOARD_TYPE device and registers a read callback
 * that polls the device through its KeyboardApi.
 *
 * @warning Caller must hold the LVGL lock (see lvgl_lock() in lvgl_module.h) — call this from
 * LvglModuleConfig.on_start, or after calling lvgl_lock() explicitly.
 *
 * @param[in] device a device of type KEYBOARD_TYPE
 * @param[in] display the display this indev should be associated with, or NULL to leave it unset
 * @param[out] out_indev the created indev, valid only when ERROR_NONE is returned
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT if device or out_indev is NULL, or device is not of type KEYBOARD_TYPE
 * @retval ERROR_OUT_OF_MEMORY if allocation failed
 */
error_t lvgl_keyboard_add(struct Device* device, lv_display_t* display, lv_indev_t** out_indev);

/**
 * @brief Removes an indev previously created with lvgl_keyboard_add().
 * @warning Caller must hold the LVGL lock.
 */
void lvgl_keyboard_remove(lv_indev_t* indev);

#ifdef __cplusplus
}
#endif
