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

    return dropdown;
}

}

#endif // ESP_PLATFORM