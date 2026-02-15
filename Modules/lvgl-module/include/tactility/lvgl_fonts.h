// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum LvglFontSize {
    FONT_SIZE_SMALL,
    FONT_SIZE_DEFAULT,
    FONT_SIZE_LARGE,
};

const lv_font_t* lvgl_get_shared_icon_font();
uint32_t lvgl_get_shared_icon_font_height();

const lv_font_t* lvgl_get_text_font(enum LvglFontSize font_size);
uint32_t lvgl_get_text_font_height(enum LvglFontSize font_size);

const lv_font_t* lvgl_get_launcher_icon_font();
uint32_t lvgl_get_launcher_icon_font_height();

const lv_font_t* lvgl_get_statusbar_icon_font();
uint32_t lvgl_get_statusbar_icon_font_height();

#ifdef __cplusplus
}
#endif
