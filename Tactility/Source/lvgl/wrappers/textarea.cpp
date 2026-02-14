#ifdef ESP_PLATFORM

#include <Tactility/app/App.h>
#include <Tactility/service/gui/GuiService.h>
#include <Tactility/Tactility.h>

extern "C" {

extern lv_obj_t* __real_lv_textarea_create(lv_obj_t* parent);

lv_obj_t* __wrap_lv_textarea_create(lv_obj_t* parent) {
    auto textarea = __real_lv_textarea_create(parent);

    if (tt::hal::getConfiguration()->uiDensity == tt::hal::UiDensity::Compact) {
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