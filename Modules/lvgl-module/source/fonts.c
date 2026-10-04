// SPDX-License-Identifier: Apache-2.0
#include <lvgl.h>
#include <lvgl/fonts.h>
#include <tactility/check.h>

// The preprocessor definitions that are used below are defined in the CMakeLists.txt from this module.

extern const lv_font_t TT_LVGL_TEXT_FONT_SMALL_SYMBOL;
extern const lv_font_t TT_LVGL_TEXT_FONT_DEFAULT_SYMBOL;
extern const lv_font_t TT_LVGL_TEXT_FONT_LARGE_SYMBOL;

static const lv_font_t* icon_fonts[LVGL_ICON_FONT_SHARED_2X + 1];

void lvgl_set_icon_font(enum LvglIconFont icon_font, const lv_font_t* font) {
    icon_fonts[icon_font] = font;
}

static const lv_font_t* get_icon_font(enum LvglIconFont icon_font) {
    const lv_font_t* font = icon_fonts[icon_font];
    return font != NULL ? font : &TT_LVGL_TEXT_FONT_DEFAULT_SYMBOL;
}

uint32_t lvgl_get_text_font_height(enum LvglFontSize font_size) {
    switch (font_size) {
        case FONT_SIZE_SMALL: return TT_LVGL_TEXT_FONT_SMALL_SIZE;
        case FONT_SIZE_DEFAULT: return TT_LVGL_TEXT_FONT_DEFAULT_SIZE;
        case FONT_SIZE_LARGE: return TT_LVGL_TEXT_FONT_LARGE_SIZE;
        default: check(false);
    }
}
const lv_font_t* lvgl_get_text_font(enum LvglFontSize font_size) {
    switch (font_size) {
        case FONT_SIZE_SMALL: return &TT_LVGL_TEXT_FONT_SMALL_SYMBOL;
        case FONT_SIZE_DEFAULT: return &TT_LVGL_TEXT_FONT_DEFAULT_SYMBOL;
        case FONT_SIZE_LARGE: return &TT_LVGL_TEXT_FONT_LARGE_SYMBOL;
        default: check(false);
    }
}

uint32_t lvgl_get_shared_icon_font_height() { return TT_LVGL_SHARED_FONT_ICON_SIZE; }

const lv_font_t* lvgl_get_shared_icon_font() { return get_icon_font(LVGL_ICON_FONT_SHARED); }

uint32_t lvgl_get_shared_icon_font_2x_height() { return TT_LVGL_SHARED_FONT_ICON_SIZE * 2; }

const lv_font_t* lvgl_get_shared_icon_font_2x() { return get_icon_font(LVGL_ICON_FONT_SHARED_2X); }

uint32_t lvgl_get_launcher_icon_font_height() { return TT_LVGL_LAUNCHER_FONT_ICON_SIZE; }

const lv_font_t* lvgl_get_launcher_icon_font() { return get_icon_font(LVGL_ICON_FONT_LAUNCHER); }

uint32_t lvgl_get_statusbar_icon_font_height() { return TT_LVGL_STATUSBAR_FONT_ICON_SIZE; }

const lv_font_t* lvgl_get_statusbar_icon_font() { return get_icon_font(LVGL_ICON_FONT_STATUSBAR); }
