// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a QR code view. It draws the code with square modules of a whole number of pixels,
 * centered in the object and surrounded by the quiet zone that scanners need.
 * Give it a size, and set the data with lvgl_qr_set_data().
 * @param[in] parent the parent object for the new view
 * @return the created view
 */
lv_obj_t* lvgl_qr_create(lv_obj_t* parent);

/**
 * @brief Sets the text or bytes to encode, in the smallest QR version that fits (low error correction).
 * The view keeps showing the previous code when this fails.
 * @param[in] obj a view created with lvgl_qr_create()
 * @param[in] data the NUL-terminated data, at most 1273 bytes
 * @retval ERROR_NONE on success
 * @retval ERROR_OUT_OF_RANGE when the data doesn't fit in a QR code
 * @retval ERROR_OUT_OF_MEMORY when the code couldn't be allocated
 */
error_t lvgl_qr_set_data(lv_obj_t* obj, const char* data);

/**
 * @brief The smallest width and height in pixels that draws the code, with a module of 1 pixel and the quiet zone.
 * The view draws nothing when its content area is smaller.
 * @param[in] obj a view created with lvgl_qr_create()
 * @return the size, or 0 while no data was set
 */
int32_t lvgl_qr_get_min_size(lv_obj_t* obj);

/**
 * @brief Sets the colors of the modules and of the background, including the quiet zone.
 * Scanners need a dark code on a light background, so these are black and white by default.
 */
void lvgl_qr_set_colors(lv_obj_t* obj, lv_color_t dark, lv_color_t light);

#ifdef __cplusplus
}
#endif
