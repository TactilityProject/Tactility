// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates an outlined button for a segment of a path bar in a toolbar.
 * It behaves like a regular button with less padding. When it has LV_STATE_DISABLED it is shown as text.
 * @param[in] parent the parent object for the new button
 * @return the created button
 */
lv_obj_t* lvgl_pathbar_button_create(lv_obj_t* parent);

#ifdef __cplusplus
}
#endif
