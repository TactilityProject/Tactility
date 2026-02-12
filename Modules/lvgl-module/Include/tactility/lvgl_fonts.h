#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t material_symbols_statusbar_20;

extern const lv_font_t material_symbols_shared_16;

//#define lvgl_get_text_font_smaller() &lv_font_montserrat_12
// TODO: Make function so it's easier to use this cross-platform
#define LVGL_TEXT_FONT_DEFAULT &lv_font_montserrat_14
#define LVGL_TEXT_FONT_LARGER &lv_font_montserrat_18

// TODO: Make function so it's easier to use this cross-platform
#define LVGL_SYMBOL_FONT_DEFAULT &material_symbols_shared_16

// TODO: Make function so it's easier to use this cross-platform
#define LVGL_TOPBAR_FONT &material_symbols_statusbar_20
#define LVGL_TOPBAR_ICON_HEIGHT 20

#ifdef __cplusplus
}
#endif
