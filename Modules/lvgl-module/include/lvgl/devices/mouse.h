// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#include <tactility/device.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a pointer lv_indev_t with a cursor, bound to the given MOUSE_TYPE device.
 *
 * @warning Caller must hold the LVGL lock.
 *
 * @note Idempotent per device: if an indev is already bound to this device (see
 * lvgl_mouse_find_by_device()), that indev is returned instead of creating a second one.
 *
 * @param[in] device a device of type MOUSE_TYPE
 * @param[in] display the display this indev should be associated with, or NULL to leave it unset
 * @param[out] out_indev the created (or already-existing) indev, valid only when ERROR_NONE is returned
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT if device or out_indev is NULL, or device is not of type MOUSE_TYPE
 * @retval ERROR_OUT_OF_MEMORY if allocation failed
 */
error_t lvgl_mouse_add(struct Device* device, lv_display_t* display, lv_indev_t** out_indev);

/**
 * @brief Removes an indev previously created with lvgl_mouse_add() and its cursor.
 * @warning Caller must hold the LVGL lock.
 */
void lvgl_mouse_remove(lv_indev_t* indev);

/**
 * @brief Finds the indev previously created with lvgl_mouse_add() for the given device, if any.
 * @warning Caller must hold the LVGL lock.
 * @return the bound indev, or NULL if none is bound to this device
 */
lv_indev_t* lvgl_mouse_find_by_device(struct Device* device);

#ifdef __cplusplus
}
#endif
