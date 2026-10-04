// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Changes whenever the icon TTF subset, its codepoints file or the icon name lists change */
#define LVGL_ICON_FONT_VERSION 0xEE67835Du

// Icon names per icon font, resolved through the .codepoints file next to the TTF
extern const char* const lvgl_icon_shared_names[];
extern const size_t lvgl_icon_shared_name_count;

extern const char* const lvgl_icon_statusbar_names[];
extern const size_t lvgl_icon_statusbar_name_count;

extern const char* const lvgl_icon_launcher_names[];
extern const size_t lvgl_icon_launcher_name_count;

#ifdef __cplusplus
}
#endif
