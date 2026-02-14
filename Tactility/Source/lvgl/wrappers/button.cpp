#ifdef ESP_PLATFORM

#include <Tactility/Tactility.h>

#include <lvgl.h>

extern "C" {

extern lv_obj_t* __real_lv_button_create(lv_obj_t* parent);

lv_obj_t* __wrap_lv_button_create(lv_obj_t* parent) {
    auto button = __real_lv_button_create(parent);

    if (tt::hal::getConfiguration()->uiDensity == tt::hal::UiDensity::Compact) {
        lv_obj_set_style_pad_all(button, 2, LV_STATE_DEFAULT);
        lv_obj_set_style_radius(button, 3, LV_STATE_DEFAULT);
    }

    return button;
}

}

#endif // ESP_PLATFORM