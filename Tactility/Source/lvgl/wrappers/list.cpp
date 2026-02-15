#ifdef ESP_PLATFORM

#include <lvgl.h>

#include <tactility/lvgl_module.h>

extern "C" {

extern lv_obj_t* __real_lv_list_create(lv_obj_t* parent);
extern lv_obj_t* __real_lv_list_add_button(lv_obj_t* list, const void* icon, const char* txt);

lv_obj_t* __wrap_lv_list_create(lv_obj_t* parent) {
    auto* list = __real_lv_list_create(parent);

    if (lvgl_get_ui_density() == LVGL_UI_DENSITY_COMPACT) {
        lv_obj_set_style_pad_row(list, 2, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_column(list, 2, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(list, 2, LV_STATE_DEFAULT);
    }

    return list;
}

lv_obj_t* __wrap_lv_list_add_button(lv_obj_t* list, const void* icon, const char* txt) {
    auto* button = __real_lv_list_add_button(list, icon, txt);

    if (lvgl_get_ui_density() == LVGL_UI_DENSITY_COMPACT) {
        lv_obj_set_style_pad_ver(button, 2, LV_STATE_DEFAULT);
    }

    return button;
}

}

#endif // ESP_PLATFORM