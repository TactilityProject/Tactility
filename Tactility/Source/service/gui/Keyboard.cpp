#include "Tactility/lvgl/Keyboard.h"
#include "Tactility/Check.h"
#include "Tactility/lvgl/LvglSync.h"
#include "Tactility/service/gui/GuiService.h"

#include <Tactility/TactilityConfig.h>
#include <Tactility/service/espnow/EspNowService.h>

namespace tt::service::gui {

static void show_keyboard(lv_event_t* event) {
    auto service = findService();
    if (service != nullptr) {
        lv_obj_t* target = lv_event_get_current_target_obj(event);
        service->softwareKeyboardShow(target);
        lv_obj_scroll_to_view(target, LV_ANIM_ON);
    }
}

static void hide_keyboard(lv_event_t* event) {
    auto service = findService();
    if (service != nullptr) {
        service->softwareKeyboardHide();
    }
}

bool GuiService::softwareKeyboardIsEnabled() {
    return !lvgl::hardware_keyboard_is_available() || TT_CONFIG_FORCE_ONSCREEN_KEYBOARD;
}

void GuiService::softwareKeyboardShow(lv_obj_t* textarea) {
    lock();

    if (isStarted && keyboard != nullptr) {
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(keyboard, textarea);
    }

    unlock();
}

void GuiService::softwareKeyboardHide() {
    lock();

    if (isStarted && keyboard != nullptr) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    unlock();
}

void GuiService::keyboardAddTextArea(lv_obj_t* textarea) {
    lock();

    if (isStarted) {
        check(lvgl::lock(0), "lvgl should already be locked before calling this method");

        if (softwareKeyboardIsEnabled()) {
            lv_obj_add_event_cb(textarea, show_keyboard, LV_EVENT_FOCUSED, nullptr);
            lv_obj_add_event_cb(textarea, hide_keyboard, LV_EVENT_DEFOCUSED, nullptr);
            lv_obj_add_event_cb(textarea, hide_keyboard, LV_EVENT_READY, nullptr);

            // lv_obj_t auto-remove themselves from the group when they are destroyed (last checked in LVGL 8.3)
            lv_group_add_obj(keyboardGroup, textarea);

            lvgl::software_keyboard_activate(keyboardGroup);
        }

        lvgl::unlock();
    }

    unlock();
}

} // namespace
