// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl.h>

#include <tactility/device.h>
#include <tactility/error.h>

/**
 * @brief Linear per-axis calibration range for raw pointer coordinates.
 *
 * Values are the raw (pre-calibration) coordinates that should map to the display's
 * [0, hor_res-1] / [0, ver_res-1] range. Corrects scale+offset error only; axis
 * swap/mirror is handled separately by PointerApi and applied by the driver before
 * lvgl_pointer_read_cb() sees the coordinates.
 */
struct LvglPointerCalibration {
    int32_t x_min;
    int32_t x_max;
    int32_t y_min;
    int32_t y_max;
};

/**
 * @brief Sets (or clears, when calibration is NULL) the calibration applied to raw coordinates
 * read from the device before they are written into LVGL indev data, on an indev previously
 * created with lvgl_pointer_add().
 *
 * @warning Caller must hold the LVGL lock (see lvgl_lock() in lvgl_module.h).
 *
 * @param[in] indev an indev previously created by lvgl_pointer_add()
 * @param[in] calibration the calibration range to apply, or NULL to clear/disable calibration
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT if indev is NULL, or calibration is non-NULL but invalid
 *         (x_max <= x_min, y_max <= y_min, or either span smaller than the minimum allowed range)
 */
error_t lvgl_pointer_set_calibration(lv_indev_t* indev, const struct LvglPointerCalibration* calibration);

/**
 * @brief Retrieves the calibration currently active on indev, if any.
 * @warning Caller must hold the LVGL lock.
 * @return true when a calibration is currently set on indev (out_calibration is filled), false otherwise
 */
bool lvgl_pointer_get_calibration(lv_indev_t* indev, struct LvglPointerCalibration* out_calibration);

/**
 * @brief Returns the first indev created by lvgl_pointer_add() that hasn't been removed yet.
 *
 * Unlike iterating LVGL's own indev list, this only ever returns an indev created by
 * lvgl_pointer_add() — safe to pass to lvgl_pointer_set_calibration()/lvgl_pointer_get_calibration()
 * without risking a foreign indev (e.g. one registered by the deprecated HAL layer) whose driver
 * data isn't a struct LvglPointerCtx*.
 *
 * @warning Caller must hold the LVGL lock.
 * @return the indev, or NULL if none is currently registered.
 */
lv_indev_t* lvgl_pointer_get_default(void);

/**
 * @brief Creates an lv_indev_t bound to the given POINTER_TYPE device and registers a read callback
 * that polls the device through its PointerApi.
 *
 * @warning Caller must hold the LVGL lock (see lvgl_lock() in lvgl_module.h) — call this from
 * LvglModuleConfig.on_start, or after calling lvgl_lock() explicitly.
 *
 * @param[in] device a device of type POINTER_TYPE
 * @param[in] display the display this indev should be associated with, or NULL to leave it unset
 * @param[out] out_indev the created indev, valid only when ERROR_NONE is returned
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT if device or out_indev is NULL, or device is not of type POINTER_TYPE
 * @retval ERROR_OUT_OF_MEMORY if allocation failed
 */
error_t lvgl_pointer_add(struct Device* device, lv_display_t* display, lv_indev_t** out_indev);

/**
 * @brief Removes an indev previously created with lvgl_pointer_add().
 * @warning Caller must hold the LVGL lock.
 */
void lvgl_pointer_remove(lv_indev_t* indev);

#ifdef __cplusplus
}
#endif
