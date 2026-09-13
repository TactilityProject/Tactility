// SPDX-License-Identifier: Apache-2.0
#include "cl32_v4.h"
#include "cl32_v4_keyboard.h"
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <new>

extern Module cl32_module;

constexpr auto* TAG = "cl32-v4-keyboard";

// v4's core chip reports keyboard events on the same I2C address as the power-supply function
// (see cl32_v4_power.h) - CL-32/CL-32's CL32_core class talks to both over one address.
// Registers below come from that project's regMap.h (CL32_INTERRUPT/EVENT_COUNT/EVENT1).
constexpr uint8_t REG_INTERRUPT = 0x02;   // bit0: keyboard event pending
constexpr uint8_t REG_EVENT_COUNT = 0x03;
constexpr uint8_t REG_EVENT1 = 0x04;      // reading this register pops one event off the chip's FIFO
constexpr uint8_t INTERRUPT_BIT_KEYBOARD = 0x01;

// Upper bound on events drained per read_key() call, same purpose as tca8418.cpp's equivalent:
// caps the damage if the chip ever reports a count that never reaches 0.
constexpr uint8_t MAX_EVENTS_PER_DRAIN = 16;

// HID Keyboard/Keypad usage codes (USB HID Usage Tables page 0x07) this chip's matrix produces,
// named after CL-32/CL-32's CL32_core::_matrix keymap (CL32_core.cpp).
constexpr uint8_t KEY_FN = 0x3A;      // repurposed F1 position
constexpr uint8_t KEY_FILE = 0x43;    // repurposed F10 position
constexpr uint8_t KEY_MENU = 0x44;    // repurposed F11 position
constexpr uint8_t KEY_ENTER = 0x28;
constexpr uint8_t KEY_ESCAPE = 0x29;
constexpr uint8_t KEY_BACKSPACE = 0x2A;
constexpr uint8_t KEY_SPACE = 0x2C;
constexpr uint8_t KEY_EQUALS = 0x2E;
constexpr uint8_t KEY_PERIOD = 0x37;
constexpr uint8_t KEY_ARROW_RIGHT = 0x4F;
constexpr uint8_t KEY_ARROW_LEFT = 0x50;
constexpr uint8_t KEY_ARROW_DOWN = 0x51;
constexpr uint8_t KEY_ARROW_UP = 0x52;
constexpr uint8_t KEYPAD_SLASH = 0x54;
constexpr uint8_t KEYPAD_ASTERISK = 0x55;
constexpr uint8_t KEYPAD_MINUS = 0x56;
constexpr uint8_t KEYPAD_PLUS = 0x57;
constexpr uint8_t KEYPAD_ENTER = 0x58;
constexpr uint8_t KEYPAD_1 = 0x59;
constexpr uint8_t KEYPAD_9 = 0x61;
constexpr uint8_t KEYPAD_0 = 0x62;
constexpr uint8_t KEYPAD_PERIOD = 0x63;
constexpr uint8_t KEY_SHIFT = 0xE1;

enum class ToggleState { Unpressed, OnePress, Locked };

static ToggleState next_toggle_state(ToggleState state) {
    switch (state) {
        case ToggleState::Unpressed: return ToggleState::OnePress;
        case ToggleState::OnePress: return ToggleState::Locked;
        case ToggleState::Locked: return ToggleState::Unpressed;
    }
    return ToggleState::Unpressed;
}

static const char* toggle_state_name(ToggleState state) {
    switch (state) {
        case ToggleState::Unpressed: return "unpressed";
        case ToggleState::OnePress: return "one-press";
        case ToggleState::Locked: return "locked";
    }
    return "?";
}

constexpr uint8_t PENDING_CAPACITY = 32;

struct Cl32V4KeyEvent {
    uint32_t key;
    bool pressed;
    uint8_t hid_keycode;
    uint8_t hid_modifier;
};

struct Cl32V4KeyboardInternal {
    ToggleState shift_state = ToggleState::Unpressed;
    ToggleState fn_state = ToggleState::Unpressed;
    Cl32V4KeyEvent pending[PENDING_CAPACITY];
    uint8_t pending_head = 0;
    uint8_t pending_count = 0;
};

static void push_pending(Cl32V4KeyboardInternal* internal, uint32_t key, bool pressed, uint8_t hid_keycode, uint8_t hid_modifier) {
    if (internal->pending_count >= PENDING_CAPACITY) {
        LOG_W(TAG, "Pending event queue full, dropping event");
        return;
    }
    uint8_t tail = (internal->pending_head + internal->pending_count) % PENDING_CAPACITY;
    internal->pending[tail] = { key, pressed, hid_keycode, hid_modifier };
    internal->pending_count++;
}

static bool pop_pending(Cl32V4KeyboardInternal* internal, Cl32V4KeyEvent* out_event) {
    if (internal->pending_count == 0) {
        return false;
    }
    *out_event = internal->pending[internal->pending_head];
    internal->pending_head = (internal->pending_head + 1) % PENDING_CAPACITY;
    internal->pending_count--;
    return true;
}

// Maps a HID code to a Unicode codepoint, mirroring CL32_core::_matrix. Fn selects the symbol
// layer (that table's third column); Shift selects uppercase. Returns false for a code with no
// Unicode/LVGL representation (Page Up/Down) - callers report KeyboardKeyData::hid_keycode
// instead, same as tab5_keyboard.cpp's F1-F12 handling.
static bool translate_key(uint8_t code, bool shift, bool fn, uint32_t* out_key) {
    if (code >= 0x04 && code <= 0x1D) {
        if (fn) {
            static constexpr uint32_t FN_SYMBOLS[26] = {
                '|', '>', '~', ',', '@', '?', '\'', ':', '&', ';', '{', '}', ']', '[',
                '(', ')', '!', 0x00A3 /* GBP sign */, '#', '$', '^', '<', '"', '\\', '%', 0,
            };
            uint32_t symbol = FN_SYMBOLS[code - 0x04];
            if (symbol != 0) {
                *out_key = symbol;
                return true;
            }
            // 'z' has no symbol layer in the reference matrix - fall through to the plain letter.
        }
        uint32_t c = 'a' + (code - 0x04);
        *out_key = shift ? (c - 0x20) : c;
        return true;
    }

    if (fn) {
        switch (code) {
            case KEY_ARROW_RIGHT: *out_key = CODEPOINT_END; return true;
            case KEY_ARROW_LEFT: *out_key = CODEPOINT_HOME; return true;
            case KEY_ARROW_DOWN: return false; // Page Down - hid_keycode only
            case KEY_ARROW_UP: return false;   // Page Up - hid_keycode only
            default: break;
        }
    }

    switch (code) {
        case KEY_ENTER: *out_key = CODEPOINT_ENTER; return true;
        case KEY_ESCAPE: *out_key = CODEPOINT_ESCAPE; return true;
        case KEY_BACKSPACE: *out_key = CODEPOINT_BACKSPACE; return true;
        case KEY_SPACE: *out_key = ' '; return true;
        case KEY_EQUALS: *out_key = '='; return true;
        case KEY_PERIOD: *out_key = '.'; return true;
        case KEY_ARROW_RIGHT: *out_key = CODEPOINT_ARROW_RIGHT; return true;
        case KEY_ARROW_LEFT: *out_key = CODEPOINT_ARROW_LEFT; return true;
        case KEY_ARROW_DOWN: *out_key = CODEPOINT_ARROW_DOWN; return true;
        case KEY_ARROW_UP: *out_key = CODEPOINT_ARROW_UP; return true;
        case KEYPAD_SLASH: *out_key = '/'; return true;
        case KEYPAD_ASTERISK: *out_key = '*'; return true;
        case KEYPAD_MINUS: *out_key = '-'; return true;
        case KEYPAD_PLUS: *out_key = '+'; return true;
        case KEYPAD_ENTER: *out_key = CODEPOINT_ENTER; return true;
        case KEYPAD_0: *out_key = '0'; return true;
        case KEYPAD_PERIOD: *out_key = '.'; return true;
        default:
            if (code >= KEYPAD_1 && code <= KEYPAD_9) {
                *out_key = static_cast<uint32_t>('1' + (code - KEYPAD_1));
                return true;
            }
            return false;
    }
}

static void handle_key_down(Cl32V4KeyboardInternal* internal, uint8_t code) {
    if (code == KEY_SHIFT) {
        internal->shift_state = next_toggle_state(internal->shift_state);
        LOG_I(TAG, "code=0x%02X shift -> %s", code, toggle_state_name(internal->shift_state));
        return;
    }
    if (code == KEY_FN) {
        internal->fn_state = next_toggle_state(internal->fn_state);
        LOG_I(TAG, "code=0x%02X fn -> %s", code, toggle_state_name(internal->fn_state));
        return;
    }

    const bool shift = internal->shift_state != ToggleState::Unpressed;
    const uint8_t hid_modifier = shift ? 0x02 : 0x00; // bit1 = LeftShift

    // No app-level menu system at the driver layer - surface the raw HID code only, same as
    // tab5_keyboard.cpp's F1-F12 handling.
    if (code == KEY_MENU || code == KEY_FILE) {
        LOG_I(TAG, "code=0x%02X (menu/file) shift=%s fn=%s hid_modifier=0x%02X",
              code, toggle_state_name(internal->shift_state), toggle_state_name(internal->fn_state), hid_modifier);
        push_pending(internal, 0, true, code, hid_modifier);
        push_pending(internal, 0, false, code, hid_modifier);
        return;
    }

    const bool fn = internal->fn_state != ToggleState::Unpressed;
    uint32_t key = 0;
    const bool has_key = translate_key(code, shift, fn, &key);

    const char printable = (has_key && key >= 0x20 && key < 0x7F) ? static_cast<char>(key) : '.';
    LOG_I(TAG, "code=0x%02X key='%c' key_hex=0x%04X shift=%s fn=%s hid_modifier=0x%02X",
          code, printable, static_cast<unsigned int>(has_key ? key : 0),
          toggle_state_name(internal->shift_state), toggle_state_name(internal->fn_state), hid_modifier);

    // LVGL only registers a key on a RELEASED->PRESSED edge, and this chip doesn't report
    // reliable release events for character keys - queue an immediate press+release pair, same as
    // tca8418.cpp.
    push_pending(internal, has_key ? key : 0, true, code, hid_modifier);
    push_pending(internal, has_key ? key : 0, false, code, hid_modifier);

    if (internal->shift_state == ToggleState::OnePress) {
        internal->shift_state = ToggleState::Unpressed;
    }
    if (internal->fn_state == ToggleState::OnePress) {
        internal->fn_state = ToggleState::Unpressed;
    }
}

static void drain_events(Device* i2c0, Cl32V4KeyboardInternal* internal) {
    uint8_t interrupt_status = 0;
    if (i2c_controller_register8_get(i2c0, CL32_V4_CORE_I2C_ADDRESS, REG_INTERRUPT, &interrupt_status, CL32_V4_CORE_TIMEOUT) != ERROR_NONE) {
        return;
    }
    if ((interrupt_status & INTERRUPT_BIT_KEYBOARD) == 0) {
        return;
    }

    uint8_t count = 0;
    if (i2c_controller_register8_get(i2c0, CL32_V4_CORE_I2C_ADDRESS, REG_EVENT_COUNT, &count, CL32_V4_CORE_TIMEOUT) != ERROR_NONE) {
        return;
    }

    for (uint8_t drained = 0; drained < MAX_EVENTS_PER_DRAIN && count > 0; drained++) {
        uint8_t raw = 0;
        if (i2c_controller_register8_get(i2c0, CL32_V4_CORE_I2C_ADDRESS, REG_EVENT1, &raw, CL32_V4_CORE_TIMEOUT) != ERROR_NONE) {
            break;
        }

        LOG_I(TAG, "raw event=0x%02X (%s)", raw, (raw & 0x80) != 0 ? "down" : "up");

        if ((raw & 0x80) != 0) {
            handle_key_down(internal, static_cast<uint8_t>(raw & 0x7F));
        }

        if (i2c_controller_register8_get(i2c0, CL32_V4_CORE_I2C_ADDRESS, REG_EVENT_COUNT, &count, CL32_V4_CORE_TIMEOUT) != ERROR_NONE) {
            break;
        }
    }

    // Only the keyboard bit is ours to clear - the power-supply function on this same chip owns
    // the other status bits in this register.
    i2c_controller_register8_reset_bits(i2c0, CL32_V4_CORE_I2C_ADDRESS, REG_INTERRUPT, INTERRUPT_BIT_KEYBOARD, CL32_V4_CORE_TIMEOUT);
}

static error_t v3_read_key(Device* device, KeyboardKeyData* data) {
    auto* internal = static_cast<Cl32V4KeyboardInternal*>(device_get_driver_data(device));

    Cl32V4KeyEvent event;
    if (!pop_pending(internal, &event)) {
        drain_events(device_get_parent(device), internal);
    }

    if (pop_pending(internal, &event)) {
        data->key = event.key;
        data->pressed = event.pressed;
        data->continue_reading = internal->pending_count > 0;
        data->ctrl = false;
        data->alt = false;
        data->hid_keycode = event.hid_keycode;
        data->hid_modifier = event.hid_modifier;
    } else {
        data->key = 0;
        data->pressed = false;
        data->continue_reading = false;
        data->ctrl = false;
        data->alt = false;
        data->hid_keycode = 0;
        data->hid_modifier = 0;
    }

    return ERROR_NONE;
}

static constexpr KeyboardApi cl32_v4_keyboard_api = {
    .read_key = v3_read_key,
    .get_backlight = nullptr,
    .is_present = nullptr,
};

static error_t start(Device* device) {
    if (device_get_type(device_get_parent(device)) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto* internal = new(std::nothrow) Cl32V4KeyboardInternal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Cl32V4KeyboardInternal*>(device_get_driver_data(device));
    delete internal;
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

Driver cl32_v4_keyboard_driver = {
    .name = "cl32-v4-keyboard",
    .compatible = (const char*[]) { "cl32-v4-keyboard", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &cl32_v4_keyboard_api,
    .device_type = &KEYBOARD_TYPE,
    .owner = &cl32_module,
    .internal = nullptr
};

static Device cl32_v4_keyboard_device {};

void cl32_v4_create_keyboard(Device* i2c0) {
    cl32_v4_keyboard_device = Device { .address = 0, .name = "cl32-v4-keyboard", .config = nullptr, .parent = nullptr, .flags = 0, .internal = nullptr };

    error_t error = device_construct(&cl32_v4_keyboard_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to construct keyboard: %s", error_to_string(error));
        return;
    }

    device_set_parent(&cl32_v4_keyboard_device, i2c0);
    device_set_driver(&cl32_v4_keyboard_device, &cl32_v4_keyboard_driver);

    error = device_add(&cl32_v4_keyboard_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to add keyboard: %s", error_to_string(error));
        device_destruct(&cl32_v4_keyboard_device);
        return;
    }

    error = device_start(&cl32_v4_keyboard_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to start keyboard: %s", error_to_string(error));
        device_remove(&cl32_v4_keyboard_device);
        device_destruct(&cl32_v4_keyboard_device);
        return;
    }
}
