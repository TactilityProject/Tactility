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

    return widget;
}

}

#endif // ESP_PLATFORM
