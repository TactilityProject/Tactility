// SPDX-License-Identifier: Apache-2.0
#include <lvgl/devices/keyboard.h>
#include <lvgl/devices/device_context.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>

#include <tactility/drivers/keyboard.h>
#include <tactility/log.h>

#include <vector>

constexpr auto* TAG = "lvgl_keyboard";

static LvglSoftwareKeyboard last_software_keyboard = {
    .object = nullptr
};

static lv_group_t* keyboard_group;

extern "C" {

void lvgl_keyboard_on_start_lvgl() {
    lvgl_lock();
    keyboard_group = lv_group_create();
    check(keyboard_group);
    // We currently set this group as the default, so it doesn't only get (manually added) textareas,
    // but gets all widgets by default. This is a temporary work-around until a proper default group is
    // created to fix the trackball issue (see trackball.cpp and ideas.md, search for "group")
    lv_group_set_default(keyboard_group);
    lvgl_unlock();
}

void lvgl_keyboard_on_stop_lvgl() {
    last_software_keyboard = {
        .object = nullptr
    };

    lvgl_lock();
    lv_group_delete(keyboard_group);
    lvgl_unlock();

    keyboard_group = nullptr;
}

// KeyboardKeyData::key is always a Unicode codepoint (see its doc comment / the CodePoint enum) -
// drivers never emit LV_KEY_* directly, including for pure focus-navigation concepts that have no
// ordinary character of their own. LVGL itself hardcodes specific sentinel values in its own
// indev/group/textarea code that don't match the real Unicode codepoint chosen for the same key,
// so those are translated here rather than each driver having to know about LVGL's internals.
// CODEPOINT_BACKSPACE/TAB/ESCAPE/DELETE already equal their LV_KEY_* counterpart numerically, so
// they need no case below - they fall through `default` unchanged.
static uint32_t codepoint_to_lv_key(uint32_t key) {
    switch (key) {
        case CODEPOINT_ENTER: return LV_KEY_ENTER;
        case CODEPOINT_ARROW_LEFT: return LV_KEY_LEFT;
        case CODEPOINT_ARROW_UP: return LV_KEY_PREV;
        case CODEPOINT_ARROW_RIGHT: return LV_KEY_RIGHT;
        case CODEPOINT_ARROW_DOWN: return LV_KEY_NEXT;
        case CODEPOINT_FOCUS_NEXT: return LV_KEY_NEXT;
        case CODEPOINT_FOCUS_PREVIOUS: return LV_KEY_PREV;
        case CODEPOINT_HOME: return LV_KEY_HOME;
        case CODEPOINT_END: return LV_KEY_END;
        default: return key;
    }
}

static void lvgl_keyboard_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* wrapper = static_cast<LvglDeviceContext*>(lv_indev_get_driver_data(indev));

    KeyboardKeyData key_data = {};
    if (keyboard_read_key(wrapper->device, &key_data) != ERROR_NONE) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = false;
        return;
    }

    data->key = codepoint_to_lv_key(key_data.key);
    lv_group_t* group = lv_indev_get_group(indev);
    lv_obj_t* focused = group != nullptr ? lv_group_get_focused(group) : nullptr;
    if (lvgl_grid_navigation_is_container(focused)) {
        if (key_data.key == CODEPOINT_ARROW_UP) {
            // Grid navigation moves the focus in two dimensions with the arrow keys
            data->key = LV_KEY_UP;
        } else if (key_data.key == CODEPOINT_ARROW_DOWN) {
            data->key = LV_KEY_DOWN;
        } else if (key_data.key == CODEPOINT_FOCUS_NEXT || key_data.key == CODEPOINT_FOCUS_PREVIOUS) {
            // Devices that can only step through widgets visit the children in their order.
            // LVGL doesn't see these keys: the group would skip the children, as only the container is in the group.
            if (key_data.pressed) {
                lvgl_grid_navigation_focus_next(focused, key_data.key == CODEPOINT_FOCUS_NEXT);
            }
            data->state = LV_INDEV_STATE_RELEASED;
            data->continue_reading = key_data.continue_reading;
            return;
        }
    }
    data->state = key_data.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = key_data.continue_reading;
}

error_t lvgl_keyboard_add(struct Device* device, lv_display_t* display, lv_indev_t** out_indev) {
    if (device == NULL || out_indev == NULL) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (device_get_type(device) != &KEYBOARD_TYPE) {
        return ERROR_INVALID_ARGUMENT;
    }

    // A device can reach here twice: lvgl_devices_attach()'s boot scan binds every KEYBOARD_TYPE
    // device unconditionally (started or not), and a device that wasn't started yet at that point
    // fires DEVICE_EVENT_STARTED later, driving a second call through
    // KeyboardDeviceListener::onKeyboardDeviceStarted(). Without this check that would create a
    // second indev for the same device, and onKeyboardDeviceStopped() only ever removes one of
    // them, leaving the other dangling - polling a destructed device on the next LVGL tick.
    lv_indev_t* existing = lvgl_keyboard_find_by_device(device);
    if (existing != NULL) {
        *out_indev = existing;
        return ERROR_NONE;
    }

    auto* wrapper = new(std::nothrow) LvglDeviceContext(nullptr);
    if (wrapper == NULL) {
        return ERROR_OUT_OF_MEMORY;
    }
    wrapper->device = device;

    lv_indev_t* indev = lv_indev_create();
    if (indev == NULL) {
        delete wrapper;
        return ERROR_OUT_OF_MEMORY;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, lvgl_keyboard_read_cb);
    lv_indev_set_driver_data(indev, wrapper);
    if (display != NULL) {
        lv_indev_set_display(indev, display);
    }

    lvgl_keyboard_enable(indev);

    *out_indev = indev;
    return ERROR_NONE;
}

void lvgl_keyboard_remove(lv_indev_t* indev) {
    if (indev == NULL) {
        return;
    }

    auto* wrapper = static_cast<LvglDeviceContext*>(lv_indev_get_driver_data(indev));
    lv_indev_delete(indev);
    delete wrapper;
}

lv_indev_t* lvgl_keyboard_find_by_device(Device* device) {
    lv_indev_t* indev = lv_indev_get_next(nullptr);
    while (indev != nullptr) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            auto* wrapper = static_cast<LvglDeviceContext*>(lv_indev_get_driver_data(indev));
            if (wrapper != nullptr && wrapper->device == device) {
                return indev;
            }
        }
        indev = lv_indev_get_next(indev);
    }
    return nullptr;
}

void lvgl_keyboard_enable(lv_indev_t* indev) {
    check(keyboard_group != nullptr);
    lv_indev_set_group(indev, keyboard_group);
}

void lvgl_keyboard_disable(lv_indev_t* indev) {
    lv_indev_set_group(indev, nullptr);
}

static bool lvgl_hardware_keyboard_check_present(Device* device, void* context) {
    bool ready = device_is_ready(device);
    bool present = ready && keyboard_is_present(device);
    LOG_D(TAG, "keyboard device %s: ready=%d present=%d", device->name, (int)ready, (int)present);
    if (!present) {
        return true; // keep looking
    }
    *static_cast<bool*>(context) = true;
    return false; // found one, stop iterating
}

bool lvgl_hardware_keyboard_is_available() {
    // TODO: Refactor the driver subsystem to so it does proper probing/releasing of such devices
    // This work-around exists for the Tab5 keyboard driver.
    bool present = false;
    device_for_each_of_type(&KEYBOARD_TYPE, &present, lvgl_hardware_keyboard_check_present);
    LOG_D(TAG, "lvgl_hardware_keyboard_is_available() -> %d", (int)present);
    return present;
}

void lvgl_hardware_keyboard_add_custom(lv_indev_t* indev) {
    auto* wrapper = new(std::nothrow) LvglDeviceContext(nullptr);
    if (wrapper == nullptr) {
        return;
    }

    lv_indev_set_driver_data(indev, wrapper);

    lvgl_keyboard_enable(indev);
}

void lvgl_hardware_keyboard_remove_custom(lv_indev_t* indev) {
    lvgl_keyboard_disable(indev);
    auto* wrapper = static_cast<LvglDeviceContext*>(lv_indev_get_driver_data(indev));
    lv_indev_set_driver_data(indev, nullptr);
    delete wrapper;
}

static void textarea_show_keyboard(lv_event_t* event) {
    // Re-checked here rather than gated once at lvgl_keyboard_add_textarea() time, so a hardware
    // keyboard that connects/disconnects after the textarea was created is honored immediately.
    if (!lvgl_software_keyboard_is_enabled()) {
        return;
    }
    lv_obj_t* target = lv_event_get_current_target_obj(event);
    if (last_software_keyboard.object != nullptr) {
        lvgl_software_keyboard_show(&last_software_keyboard, target);
        lv_obj_scroll_to_view(target, LV_ANIM_ON);
    }
}

static void textarea_hide_keyboard(lv_event_t* event) {
    if (last_software_keyboard.object == nullptr) {
        return;
    }
    // Only hide if the keyboard is actually bound to the textarea that triggered this
    lv_obj_t* target = lv_event_get_current_target_obj(event);
    if (lv_keyboard_get_textarea(last_software_keyboard.object) != target) {
        return;
    }
    lvgl_software_keyboard_hide(&last_software_keyboard);
}

void lvgl_software_keyboard_construct(LvglSoftwareKeyboard* keyboard, lv_obj_t* parent) {
    keyboard->object = lv_keyboard_create(parent);
    lv_obj_add_flag(keyboard->object, LV_OBJ_FLAG_HIDDEN);
    last_software_keyboard = *keyboard;
}

void lvgl_software_keyboard_destruct(LvglSoftwareKeyboard* keyboard) {
    check(keyboard->object);

    lv_obj_delete(keyboard->object);
    keyboard->object = nullptr;

    last_software_keyboard = *keyboard;
}

void lvgl_software_keyboard_show(LvglSoftwareKeyboard* keyboard, lv_obj_t* textarea) {
    assert(keyboard->object != nullptr);
    lv_obj_clear_flag(keyboard->object, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(keyboard->object, textarea);
}

void lvgl_software_keyboard_hide(LvglSoftwareKeyboard* keyboard) {
    assert(keyboard->object != nullptr);
    lv_obj_add_flag(keyboard->object, LV_OBJ_FLAG_HIDDEN);
}

bool lvgl_software_keyboard_is_enabled() {
    return !lvgl_hardware_keyboard_is_available();
}

LvglSoftwareKeyboard* lvgl_software_keyboard_get_last() {
    return &last_software_keyboard;
}

void lvgl_keyboard_add_textarea(LvglSoftwareKeyboard* keyboard, lv_obj_t* textarea) {
    lv_obj_add_event_cb(textarea, textarea_show_keyboard, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(textarea, textarea_hide_keyboard, LV_EVENT_DEFOCUSED, nullptr);
    lv_obj_add_event_cb(textarea, textarea_hide_keyboard, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(textarea, textarea_hide_keyboard, LV_EVENT_DELETE, nullptr);

    // lv_obj_t auto-remove themselves from the group when they are destroyed (last checked in LVGL 8.3)
    lv_group_add_obj(keyboard_group, textarea);

    lvgl_software_keyboard_activate(keyboard);
}

void lvgl_software_keyboard_activate(LvglSoftwareKeyboard* keyboard) {
    auto* indev = lv_indev_get_next(nullptr);
    check(keyboard_group);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(indev, keyboard_group);
        }
        indev = lv_indev_get_next(indev);
    }
}

void lvgl_software_keyboard_deactivate(LvglSoftwareKeyboard* keyboard) {
    auto* indev = lv_indev_get_next(nullptr);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(indev, nullptr);
        }
        indev = lv_indev_get_next(indev);
    }
}

}