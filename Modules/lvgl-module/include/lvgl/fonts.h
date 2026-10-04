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

enum LvglIconFont {
    LVGL_ICON_FONT_STATUSBAR,
    LVGL_ICON_FONT_LAUNCHER,
    LVGL_ICON_FONT_SHARED,
    LVGL_ICON_FONT_SHARED_2X,
};

/**
 * @brief Sets the font that is returned by the getter of an icon font.
 * Until an icon font is set, its getter returns the default text font.
 */
void lvgl_set_icon_font(enum LvglIconFont icon_font, const lv_font_t* font);

const lv_font_t* lvgl_get_shared_icon_font(void);
uint32_t lvgl_get_shared_icon_font_height(void);

const lv_font_t* lvgl_get_shared_icon_font_2x(void);
uint32_t lvgl_get_shared_icon_font_2x_height(void);

const lv_font_t* lvgl_get_text_font(enum LvglFontSize font_size);
uint32_t lvgl_get_text_font_height(enum LvglFontSize font_size);

const lv_font_t* lvgl_get_launcher_icon_font(void);
uint32_t lvgl_get_launcher_icon_font_height(void);

const lv_font_t* lvgl_get_statusbar_icon_font(void);
uint32_t lvgl_get_statusbar_icon_font_height(void);

#ifdef __cplusplus
}
#endif
