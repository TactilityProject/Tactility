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

#define LVGL_ICON_FONT_SHARED LVGL_ICON_FONT_SHARED_DEFAULT

enum LvglIconFont {
    LVGL_ICON_FONT_STATUSBAR,
    LVGL_ICON_FONT_LAUNCHER,
    LVGL_ICON_FONT_SHARED_DEFAULT,
    LVGL_ICON_FONT_SHARED_LARGE,
};

/**
 * @brief Sets the font that is returned by the getter of a text font.
 * Until a text font is set, its getter returns LVGL's default font.
 * @param[in] font_size the text font to set
 * @param[in] font the font
 * @param[in] height the font size in pixels, returned by lvgl_get_text_font_height()
 */
void lvgl_set_text_font(enum LvglFontSize font_size, const lv_font_t* font, uint32_t height);

/**
 * @brief Sets the font that is returned by the getter of an icon font.
 * Until an icon font is set, its getter returns LVGL's default font.
 * @param[in] icon_font the icon font to set
 * @param[in] font the font
 * @param[in] height the font size in pixels, returned by the matching height getter
 */
void lvgl_set_icon_font(enum LvglIconFont icon_font, const lv_font_t* font, uint32_t height);

const lv_font_t* lvgl_get_shared_icon_default_font(void);
uint32_t lvgl_get_shared_icon_default_font_height(void);

const lv_font_t* lvgl_get_shared_icon_large_font(void);
uint32_t lvgl_get_shared_icon_large_font_height(void);

const lv_font_t* lvgl_get_text_font(enum LvglFontSize font_size);
uint32_t lvgl_get_text_font_height(enum LvglFontSize font_size);

const lv_font_t* lvgl_get_launcher_icon_font(void);
uint32_t lvgl_get_launcher_icon_font_height(void);

const lv_font_t* lvgl_get_statusbar_icon_font(void);
uint32_t lvgl_get_statusbar_icon_font_height(void);

#ifdef __cplusplus
}
#endif
