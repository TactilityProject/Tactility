// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#include <tactility/device.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Add touchbar navigation to LVGL's system so it knows how to handle touchbar events.
 *
 * @warning Caller must hold the LVGL lock
 *
 * @note Idempotent per device so that it can be called multiple times and not leak and stuff
 *
 * @param[in] device a device of type TOUCHBAR_TYPE
 * @param[in] display the display this indev should be associated with or NULL to leave it unset
 * @param[out] out_indev the created (or already-existing) indev valid only when ERROR_NONE is returned
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT if device or out_indev is NULL or device is not of type TOUCHBAR_TYPE
 * @retval ERROR_OUT_OF_MEMORY if allocation failed
 */
error_t lvgl_touchbar_add(struct Device* device, lv_display_t* display, lv_indev_t** out_indev);

/**
 * Removes an touchbar and releases its subscription
 * @warning Caller must hold the LVGL lock
 */
void lvgl_touchbar_remove(lv_indev_t* indev);

/**
 * Finds the indev previously created with lvgl_touchbar_add() for the given device, if any.
 * @warning Caller must hold the LVGL lock
 * @return the bound indev, or NULL if none is bound to this device
 */
lv_indev_t* lvgl_touchbar_find_by_device(struct Device* device);

#ifdef __cplusplus
}
#endif
