// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @return true if the cursor position is before the first \n character, or if text is NULL */
bool lvgl_textarea_is_cursor_on_first_line(lv_obj_t* textarea);

/** @return true if the cursor position doesn't have any following \n, or if text is NULL */
bool lvgl_textarea_is_cursor_on_last_line(lv_obj_t* textarea);

#ifdef __cplusplus
}
#endif