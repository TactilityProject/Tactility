#ifdef ESP_PLATFORM

#include <lvgl.h>

#include <tactility/lvgl_module.h>

extern "C" {

extern lv_obj_t* __real_lv_dropdown_create(lv_obj_t* parent);

lv_obj_t* __wrap_lv_dropdown_create(lv_obj_t* parent) {
    auto dropdown = __real_lv_dropdown_create(parent);

    if (lvgl_get_ui_density() == LVGL_UI_DENSITY_COMPACT) {
        lv_obj_set_style_pad_all(dropdown, 2, LV_STATE_DEFAULT);
    }

    lv_obj_set_style_border_width(dropdown, 1, LV_PART_MAIN);
    if (lv_display_get_color_format(lv_obj_get_display(parent)) == LV_COLOR_FORMAT_L8) {
        lv_obj_set_style_border_color(dropdown, lv_theme_get_color_secondary(parent), LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_color(dropdown, lv_color_hex(0xFAFAFA), LV_PART_MAIN);
    }

    return dropdown;
}

}

#endif // ESP_PLATFORM