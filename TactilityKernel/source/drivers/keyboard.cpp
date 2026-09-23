// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/error.h>

#include <tactility/concurrent/mutex.h>

#define KEYBOARD_DRIVER_API(driver) ((struct KeyboardApi*)driver->api)

// USB HID Usage Tables (page 0x07) key codes used by keyboard_key_from_hid_usage(). Values
// match the HID_KEY_* constants in Espressif's usb/hid_usage_keyboard.h.
enum {
    HID_KEYCODE_ENTER         = 0x28,
    HID_KEYCODE_ESC           = 0x29,
    HID_KEYCODE_BACKSPACE     = 0x2A,
    HID_KEYCODE_TAB           = 0x2B,
    HID_KEYCODE_HOME          = 0x4A,
    HID_KEYCODE_DELETE        = 0x4C,
    HID_KEYCODE_END           = 0x4D,
    HID_KEYCODE_RIGHT         = 0x4F,
    HID_KEYCODE_LEFT          = 0x50,
    HID_KEYCODE_DOWN          = 0x51,
    HID_KEYCODE_UP            = 0x52,
    HID_KEYCODE_KEYPAD_DIV    = 0x54,
    HID_KEYCODE_KEYPAD_MUL    = 0x55,
    HID_KEYCODE_KEYPAD_SUB    = 0x56,
    HID_KEYCODE_KEYPAD_ADD    = 0x57,
    HID_KEYCODE_KEYPAD_ENTER  = 0x58,
    HID_KEYCODE_KEYPAD_1      = 0x59,
    HID_KEYCODE_KEYPAD_2      = 0x5A,
    HID_KEYCODE_KEYPAD_3      = 0x5B,
    HID_KEYCODE_KEYPAD_4      = 0x5C,
    HID_KEYCODE_KEYPAD_5      = 0x5D,
    HID_KEYCODE_KEYPAD_6      = 0x5E,
    HID_KEYCODE_KEYPAD_7      = 0x5F,
    HID_KEYCODE_KEYPAD_8      = 0x60,
    HID_KEYCODE_KEYPAD_9      = 0x61,
    HID_KEYCODE_KEYPAD_0      = 0x62,
    HID_KEYCODE_KEYPAD_DELETE = 0x63,
};

// Unshifted/shifted ASCII for usage codes 0x00-0x38, in usage-code order.
static const uint8_t hid_keycode_to_ascii[57][2] = {
    {0, 0}, {0, 0}, {0, 0}, {0, 0},
    {'a', 'A'}, {'b', 'B'}, {'c', 'C'}, {'d', 'D'}, {'e', 'E'},
    {'f', 'F'}, {'g', 'G'}, {'h', 'H'}, {'i', 'I'}, {'j', 'J'},
    {'k', 'K'}, {'l', 'L'}, {'m', 'M'}, {'n', 'N'}, {'o', 'O'},
    {'p', 'P'}, {'q', 'Q'}, {'r', 'R'}, {'s', 'S'}, {'t', 'T'},
    {'u', 'U'}, {'v', 'V'}, {'w', 'W'}, {'x', 'X'}, {'y', 'Y'},
    {'z', 'Z'},
    {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'},
    {'6', '^'}, {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'},
    {'\r', '\r'}, {0, 0}, {'\b', 0}, {'\t', '\t'}, {' ', ' '},
    {'-', '_'}, {'=', '+'}, {'[', '{'}, {']', '}'},
    {'\\', '|'}, {'\\', '|'}, {';', ':'}, {'\'', '"'},
    {'`', '~'}, {',', '<'}, {'.', '>'}, {'/', '?'},
};

// Subscriptions, filtered by `device` on fan-out.
static KeyboardEventSubscription* subscriptions = nullptr;

struct KeyboardEventMutex {
    Mutex handle {};
    KeyboardEventMutex() { mutex_construct(&handle); }
    ~KeyboardEventMutex() { mutex_destruct(&handle); }
};

// One coarse mutex is fine: fan-out is just a struct copy, never caller code.
static KeyboardEventMutex subscriptions_mutex;

// Caller must hold subscriptions_mutex.
static void fan_out_to_subscribers(Device* device, const KeyboardKeyData& data) {
    for (KeyboardEventSubscription* sub = subscriptions; sub != nullptr; sub = sub->internal.next) {
        if (sub->internal.device != device) {
            continue;
        }
        if (sub->internal.count < KEYBOARD_EVENT_QUEUE_CAPACITY) {
            uint8_t tail = (sub->internal.head + sub->internal.count) % KEYBOARD_EVENT_QUEUE_CAPACITY;
            sub->internal.queue[tail] = data;
            sub->internal.count++;
        }
    }
}

static bool try_pop(KeyboardEventSubscription* sub, KeyboardKeyData* out_data) {
    mutex_lock(&subscriptions_mutex.handle);
    bool has_event = sub->internal.count > 0;
    if (has_event) {
        *out_data = sub->internal.queue[sub->internal.head];
        sub->internal.head = (sub->internal.head + 1) % KEYBOARD_EVENT_QUEUE_CAPACITY;
        sub->internal.count--;
    }
    mutex_unlock(&subscriptions_mutex.handle);
    return has_event;
}

extern "C" {

uint32_t keyboard_key_from_hid_usage(uint8_t hid_modifier, uint8_t hid_keycode,
                                     bool caps_lock, bool num_lock) {
    bool shift = (hid_modifier & KEYBOARD_HID_MOD_LEFT_SHIFT) || (hid_modifier & KEYBOARD_HID_MOD_RIGHT_SHIFT);

    switch (hid_keycode) {
        case HID_KEYCODE_ENTER:         return CODEPOINT_ENTER;
        case HID_KEYCODE_ESC:           return CODEPOINT_ESCAPE;
        case HID_KEYCODE_BACKSPACE:     return CODEPOINT_BACKSPACE;
        case HID_KEYCODE_DELETE:        return CODEPOINT_DELETE;
        case HID_KEYCODE_TAB:           return CODEPOINT_TAB;
        case HID_KEYCODE_UP:            return CODEPOINT_ARROW_UP;
        case HID_KEYCODE_DOWN:          return CODEPOINT_ARROW_DOWN;
        case HID_KEYCODE_LEFT:          return CODEPOINT_ARROW_LEFT;
        case HID_KEYCODE_RIGHT:         return CODEPOINT_ARROW_RIGHT;
        case HID_KEYCODE_HOME:          return CODEPOINT_HOME;
        case HID_KEYCODE_END:           return CODEPOINT_END;
        case HID_KEYCODE_KEYPAD_ENTER:  return CODEPOINT_ENTER;
        case HID_KEYCODE_KEYPAD_ADD:    return '+';
        case HID_KEYCODE_KEYPAD_SUB:    return '-';
        case HID_KEYCODE_KEYPAD_MUL:    return '*';
        case HID_KEYCODE_KEYPAD_DIV:    return '/';
        case HID_KEYCODE_KEYPAD_0:      return num_lock ? (uint32_t)'0' : 0u;
        case HID_KEYCODE_KEYPAD_1:      return num_lock ? (uint32_t)'1' : (uint32_t)CODEPOINT_END;
        case HID_KEYCODE_KEYPAD_2:      return num_lock ? (uint32_t)'2' : (uint32_t)CODEPOINT_ARROW_DOWN;
        case HID_KEYCODE_KEYPAD_3:      return num_lock ? (uint32_t)'3' : 0u;
        case HID_KEYCODE_KEYPAD_4:      return num_lock ? (uint32_t)'4' : (uint32_t)CODEPOINT_ARROW_LEFT;
        case HID_KEYCODE_KEYPAD_5:      return num_lock ? (uint32_t)'5' : 0u;
        case HID_KEYCODE_KEYPAD_6:      return num_lock ? (uint32_t)'6' : (uint32_t)CODEPOINT_ARROW_RIGHT;
        case HID_KEYCODE_KEYPAD_7:      return num_lock ? (uint32_t)'7' : (uint32_t)CODEPOINT_HOME;
        case HID_KEYCODE_KEYPAD_8:      return num_lock ? (uint32_t)'8' : (uint32_t)CODEPOINT_ARROW_UP;
        case HID_KEYCODE_KEYPAD_9:      return num_lock ? (uint32_t)'9' : 0u;
        case HID_KEYCODE_KEYPAD_DELETE: return num_lock ? (uint32_t)'.' : (uint32_t)CODEPOINT_DELETE;
        default: break;
    }

    // Ctrl and Alt do not suppress the key: the modifiers are reported alongside it in
    // KeyboardKeyData (ctrl/alt), so the plain character still comes through and a consumer
    // that wants a control code derives it. See KeyboardKeyData::ctrl's doc comment.
    if (hid_keycode < (sizeof(hid_keycode_to_ascii) / sizeof(hid_keycode_to_ascii[0]))) {
        bool is_letter = (hid_keycode >= 0x04 && hid_keycode <= 0x1D);
        bool effective_shift = is_letter ? (shift ^ caps_lock) : shift;
        uint8_t ch = hid_keycode_to_ascii[hid_keycode][effective_shift ? 1 : 0];
        if (ch != 0) return (uint32_t)ch;
    }
    return 0;
}

error_t keyboard_read_key(Device* device, KeyboardKeyData* data) {
    // Under the hotplug poller a keyboard device can be constructed but not started (probe
    // hasn't confirmed presence, or it's since detached) - a sync caller like LVGL's indev still
    // polls it every tick regardless, so bail out rather than touching the driver's (possibly
    // unallocated) state.
    if (!device_is_ready(device)) {
        *data = {};
        return ERROR_INVALID_STATE;
    }

    const auto* driver = device_get_driver(device);

    if (KEYBOARD_DRIVER_API(driver)->read_key == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    // Defaulted here, not per-driver: only drivers whose hardware reports them set them.
    data->ctrl = false;
    data->alt = false;
    data->hid_keycode = 0;
    data->hid_modifier = 0;

    error_t result = KEYBOARD_DRIVER_API(driver)->read_key(device, data);
    if (result == ERROR_NONE && data->key != 0) {
        // A sync caller (e.g. LVGL) isn't the only consumer of this key.
        mutex_lock(&subscriptions_mutex.handle);
        fan_out_to_subscribers(device, *data);
        mutex_unlock(&subscriptions_mutex.handle);
    }
    return result;
}

void keyboard_emit_key(Device* device, KeyboardKeyData data) {
    mutex_lock(&subscriptions_mutex.handle);
    fan_out_to_subscribers(device, data);
    mutex_unlock(&subscriptions_mutex.handle);
}

error_t keyboard_get_backlight(Device* device, Device** backlight_device) {
    const auto* driver = device_get_driver(device);

    if (KEYBOARD_DRIVER_API(driver)->get_backlight == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }

    return KEYBOARD_DRIVER_API(driver)->get_backlight(device, backlight_device);
}

bool keyboard_is_present(Device* device) {
    const auto* driver = device_get_driver(device);

    if (KEYBOARD_DRIVER_API(driver)->is_present == nullptr) {
        return true;
    }

    return KEYBOARD_DRIVER_API(driver)->is_present(device);
}

error_t keyboard_subscribe(Device* device, KeyboardEventSubscription* sub) {
    mutex_lock(&subscriptions_mutex.handle);

    // Avoid cyclic subscription list that would loop forever
    for (auto* current = subscriptions; current != nullptr; current = current->internal.next) {
        if (current == sub) {
            mutex_unlock(&subscriptions_mutex.handle);
            return ERROR_INVALID_STATE;
        }
    }

    sub->internal.device = device;
    sub->internal.head = 0;
    sub->internal.count = 0;
    sub->internal.next = subscriptions;
    subscriptions = sub;

    mutex_unlock(&subscriptions_mutex.handle);
    return ERROR_NONE;
}

error_t keyboard_unsubscribe(Device*, KeyboardEventSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&subscriptions_mutex.handle);
    for (KeyboardEventSubscription** link = &subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    mutex_unlock(&subscriptions_mutex.handle);

    return result;
}

error_t keyboard_poll(Device* device, KeyboardEventSubscription* sub, KeyboardKeyData* out_data) {
    if (try_pop(sub, out_data)) {
        return ERROR_NONE;
    }

    // keyboard_read_key() fans the result out to every subscriber, including `sub`.
    KeyboardKeyData data;
    if (keyboard_read_key(device, &data) != ERROR_NONE || data.key == 0) {
        return ERROR_TIMEOUT;
    }

    return try_pop(sub, out_data) ? ERROR_NONE : ERROR_TIMEOUT;
}

const DeviceType KEYBOARD_TYPE {
    .name = "keyboard",
};

}
