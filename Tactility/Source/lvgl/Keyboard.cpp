#include "Tactility/lvgl/Keyboard.h"
#include "Tactility/service/gui/GuiService.h"

#include <tactility/device.h>
#include <tactility/drivers/keyboard.h>

namespace tt::lvgl {

static lv_indev_t* keyboard_device = nullptr;
static lv_group_t* pending_keyboard_group = nullptr;

void software_keyboard_show(lv_obj_t* textarea) {
    auto gui_service = service::gui::findService();
    if (gui_service != nullptr) {
        gui_service->softwareKeyboardShow(textarea);
    }
}

void software_keyboard_hide() {
    auto gui_service = service::gui::findService();
    if (gui_service != nullptr) {
        gui_service->softwareKeyboardHide();
    }
}

bool software_keyboard_is_enabled() {
    auto gui_service = service::gui::findService();
    if (gui_service != nullptr) {
        return gui_service->softwareKeyboardIsEnabled();
    } else {
        return false;
    }
}

void software_keyboard_activate(lv_group_t* group) {
    pending_keyboard_group = group;
    if (keyboard_device != nullptr) {
        lv_indev_set_group(keyboard_device, group);
    }
}

void software_keyboard_deactivate() {
    pending_keyboard_group = nullptr;
    if (keyboard_device != nullptr) {
        lv_indev_set_group(keyboard_device, nullptr);
    }
}

bool hardware_keyboard_is_available() {
    if (keyboard_device != nullptr) {
        return true;
    }
    bool has_kernel_keyboard = false;
    device_for_each_of_type(&KEYBOARD_TYPE, &has_kernel_keyboard, [](Device* device, void* context) {
        if (device_is_ready(device)) {
            *static_cast<bool*>(context) = true;
            return false;
        }
        return true;
    });
    return has_kernel_keyboard;
}

void hardware_keyboard_set_indev(lv_indev_t* device) {
    keyboard_device = device;
    // If an app already activated a keyboard group while no hardware keyboard was
    // connected, apply the pending group now that the device is available.
    if (device != nullptr && pending_keyboard_group != nullptr) {
        lv_indev_set_group(device, pending_keyboard_group);
    }
}

}
