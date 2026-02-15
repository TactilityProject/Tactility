#ifdef ESP_PLATFORM

#include <lvgl.h>

#include <tactility/lvgl_module.h>

#include <Tactility/service/gui/GuiService.h>

extern "C" {

extern lv_obj_t* __real_lv_textarea_create(lv_obj_t* parent);

lv_obj_t* __wrap_lv_textarea_create(lv_obj_t* parent) {
    auto textarea = __real_lv_textarea_create(parent);

    if (lvgl_get_ui_density() == LVGL_UI_DENSITY_COMPACT) {
        lv_obj_set_style_pad_all(textarea, 2, LV_STATE_DEFAULT);
    }

    auto gui_service = tt::service::gui::findService();
    if (gui_service != nullptr) {
        gui_service->keyboardAddTextArea(textarea);
    }

    return textarea;
}

}

#endif // ESP_PLATFORM