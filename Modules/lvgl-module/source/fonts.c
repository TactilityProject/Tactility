// SPDX-License-Identifier: Apache-2.0
#include <lvgl.h>
#include <lvgl/fonts.h>

struct RegisteredFont {
    const lv_font_t* font;
    uint32_t height;
};

static struct RegisteredFont text_fonts[FONT_SIZE_LARGE + 1];
static struct RegisteredFont icon_fonts[LVGL_ICON_FONT_SHARED_LARGE + 1];

void lvgl_set_text_font(enum LvglFontSize font_size, const lv_font_t* font, uint32_t height) {
    text_fonts[font_size] = (struct RegisteredFont) { .font = font, .height = height };
}

void lvgl_set_icon_font(enum LvglIconFont icon_font, const lv_font_t* font, uint32_t height) {
    icon_fonts[icon_font] = (struct RegisteredFont) { .font = font, .height = height };
}

static const lv_font_t* get_font(const struct RegisteredFont* registered) {
    return registered->font != NULL ? registered->font : LV_FONT_DEFAULT;
}

static uint32_t get_height(const struct RegisteredFont* registered) {
    return registered->font != NULL ? registered->height : (uint32_t)lv_font_get_line_height(LV_FONT_DEFAULT);
}

const lv_font_t* lvgl_get_text_font(enum LvglFontSize font_size) { return get_font(&text_fonts[font_size]); }

uint32_t lvgl_get_text_font_height(enum LvglFontSize font_size) { return get_height(&text_fonts[font_size]); }

const lv_font_t* lvgl_get_shared_icon_default_font() { return get_font(&icon_fonts[LVGL_ICON_FONT_SHARED_DEFAULT]); }

uint32_t lvgl_get_shared_icon_default_font_height() { return get_height(&icon_fonts[LVGL_ICON_FONT_SHARED_DEFAULT]); }

const lv_font_t* lvgl_get_shared_icon_large_font() { return get_font(&icon_fonts[LVGL_ICON_FONT_SHARED_LARGE]); }

uint32_t lvgl_get_shared_icon_large_font_height() { return get_height(&icon_fonts[LVGL_ICON_FONT_SHARED_LARGE]); }

const lv_font_t* lvgl_get_launcher_icon_font() { return get_font(&icon_fonts[LVGL_ICON_FONT_LAUNCHER]); }

uint32_t lvgl_get_launcher_icon_font_height() { return get_height(&icon_fonts[LVGL_ICON_FONT_LAUNCHER]); }

const lv_font_t* lvgl_get_statusbar_icon_font() { return get_font(&icon_fonts[LVGL_ICON_FONT_STATUSBAR]); }

uint32_t lvgl_get_statusbar_icon_font_height() { return get_height(&icon_fonts[LVGL_ICON_FONT_STATUSBAR]); }
