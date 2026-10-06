#ifdef ESP_PLATFORM

#include <lvgl/lvgl.h>
#include <lvgl/devices/keyboard.h>

extern "C" {

extern lv_obj_t* __real_lv_textarea_create(lv_obj_t* parent);

lv_obj_t* __wrap_lv_textarea_create(lv_obj_t* parent) {
    auto textarea = __real_lv_textarea_create(parent);

    auto* software_keyboard = lvgl_software_keyboard_get_last();
    if (software_keyboard != nullptr) {
        lvgl_keyboard_add_textarea(software_keyboard, textarea);
    }

    return textarea;
}

}

#endif // ESP_PLATFORM
