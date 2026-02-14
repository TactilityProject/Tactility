#ifdef ESP_PLATFORM

#include <Tactility/Tactility.h>

#include <lvgl.h>

extern "C" {

extern lv_obj_t* __real_lv_switch_create(lv_obj_t* parent);

lv_obj_t* __wrap_lv_switch_create(lv_obj_t* parent) {
    auto widget = __real_lv_switch_create(parent);

    if (tt::hal::getConfiguration()->uiDensity == tt::hal::UiDensity::Compact) {
        lv_obj_set_style_size(widget, 25, 15, LV_STATE_DEFAULT);
    }

    return widget;
}

}

#endif // ESP_PLATFORM
