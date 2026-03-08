#ifdef ESP_PLATFORM

#include <lvgl.h>

#include <tactility/lvgl_module.h>

extern "C" {

extern lv_obj_t* __real_lv_switch_create(lv_obj_t* parent);

lv_obj_t* __wrap_lv_switch_create(lv_obj_t* parent) {
    auto widget = __real_lv_switch_create(parent);

    if (lvgl_get_ui_density() == LVGL_UI_DENSITY_COMPACT) {
        lv_obj_set_style_size(widget, 25, 15, LV_STATE_DEFAULT);
    }

    if (lv_display_get_color_format(lv_obj_get_display(parent)) == LV_COLOR_FORMAT_L8) {
        lv_obj_set_style_bg_color(widget, lv_theme_get_color_secondary(parent), LV_PART_MAIN);
    }

    return widget;
}

}

#endif // ESP_PLATFORM
