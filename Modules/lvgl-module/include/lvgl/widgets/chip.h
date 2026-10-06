// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a chip: a compact button that shows a choice. The theme shows it as selected when it has LV_STATE_CHECKED.
 * The chip doesn't toggle by itself, so the app decides which chips are selected (e.g. one of several options).
 * @param[in] parent the parent object for the new chip
 * @return the created chip
 */
lv_obj_t* lvgl_chip_create(lv_obj_t* parent);

#ifdef __cplusplus
}
#endif
