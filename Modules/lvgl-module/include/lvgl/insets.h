// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#include <tactility/drivers/display.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Margins in pixels on each side of a display */
typedef struct {
    int32_t top;
    int32_t bottom;
    int32_t left;
    int32_t right;
} LvglInsets;

/**
 * @brief Gets the shape of the display's visible area.
 * @param[in] display the display
 * @param[out] out_shape the shape: a rectangle when the display doesn't report one
 */
void lvgl_display_get_shape(lv_display_t* display, struct DisplayShape* out_shape);

/**
 * @brief Gets the margins that keep a rectangle inside the display's visible area.
 * They're all zero for rectangular displays.
 * @param[in] display the display
 * @param[out] out_insets the margins
 */
void lvgl_display_get_insets(lv_display_t* display, LvglInsets* out_insets);

/**
 * @brief Gets the horizontal margin on both sides of a row, for layouts that follow the display's edge.
 * @param[in] display the display
 * @param[in] y the row, in display coordinates
 * @return the margin in pixels on the left and on the right of the row
 */
int32_t lvgl_display_get_row_inset(lv_display_t* display, int32_t y);

/**
 * @brief Moves the object's content inward where it overlaps the curved edge of a round or rounded display.
 * The extra horizontal padding is added to the object's current padding, and follows its size changes.
 * Does nothing on rectangular displays.
 * @param[in] obj the object, with its own padding already set
 */
void lvgl_obj_add_edge_padding(lv_obj_t* obj);

#ifdef __cplusplus
}
#endif
