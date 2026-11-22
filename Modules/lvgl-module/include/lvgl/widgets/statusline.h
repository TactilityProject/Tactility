// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How the status of a statusline is shown: the theme gives each level its own dot */
typedef enum {
    /** Everything works, e.g. mesh is running */
    LVGL_STATUSLINE_LEVEL_OK,
    /** Something needs attention, e.g. there is no internet connection */
    LVGL_STATUSLINE_LEVEL_WARNING,
    /** Something failed, e.g. a download */
    LVGL_STATUSLINE_LEVEL_ERROR,
} LvglStatuslineLevel;

/**
 * @brief Creates a statusline: one line of small text with a dot in front of it that shows the level of the status.
 * It's as wide as its parent and text that doesn't fit ends in an ellipsis, so it takes one line on any display.
 * It starts with LVGL_STATUSLINE_LEVEL_OK and no text.
 * @param[in] parent the parent object
 * @return the created statusline
 */
lv_obj_t* lvgl_statusline_create(lv_obj_t* parent);

/**
 * @param[in] obj the statusline
 * @param[in] text the status, copied
 */
void lvgl_statusline_set_text(lv_obj_t* obj, const char* text);

/**
 * @param[in] obj the statusline
 * @param[in] level the level of the status
 */
void lvgl_statusline_set_level(lv_obj_t* obj, LvglStatuslineLevel level);

#ifdef __cplusplus
}
#endif
