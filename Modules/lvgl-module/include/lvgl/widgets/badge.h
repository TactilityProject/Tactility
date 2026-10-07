// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a badge: a small dot that marks its parent (e.g. a favourite item).
 * The theme determines its size and colour. It ignores the parent's layout, so the app aligns it (e.g. LV_ALIGN_TOP_RIGHT).
 * @param[in] parent the object to mark
 * @return the created badge
 */
lv_obj_t* lvgl_badge_create(lv_obj_t* parent);

#ifdef __cplusplus
}
#endif
