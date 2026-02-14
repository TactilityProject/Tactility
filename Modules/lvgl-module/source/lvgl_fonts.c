#include <lvgl.h>
#include <tactility/lvgl_fonts.h>
#include <tactility/check.h>

// The preprocessor definitions that are used below are defined in the CMakeLists.txt from this module.

extern const lv_font_t TT_LVGL_TEXT_FONT_SMALL_SYMBOL;
extern const lv_font_t TT_LVGL_TEXT_FONT_DEFAULT_SYMBOL;
extern const lv_font_t TT_LVGL_TEXT_FONT_LARGE_SYMBOL;

extern const lv_font_t TT_LVGL_LAUNCHER_FONT_ICON_SYMBOL;
extern const lv_font_t TT_LVGL_STATUSBAR_FONT_ICON_SYMBOL;
extern const lv_font_t TT_LVGL_SHARED_FONT_ICON_SYMBOL;

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

const lv_font_t* lvgl_get_shared_icon_font() { return &TT_LVGL_SHARED_FONT_ICON_SYMBOL; }

uint32_t lvgl_get_launcher_icon_font_height() { return TT_LVGL_LAUNCHER_FONT_ICON_SIZE; }

const lv_font_t* lvgl_get_launcher_icon_font() { return &TT_LVGL_LAUNCHER_FONT_ICON_SYMBOL; }

uint32_t lvgl_get_statusbar_icon_font_height() { return TT_LVGL_STATUSBAR_FONT_ICON_SIZE; }

const lv_font_t* lvgl_get_statusbar_icon_font() { return &TT_LVGL_STATUSBAR_FONT_ICON_SYMBOL; }
