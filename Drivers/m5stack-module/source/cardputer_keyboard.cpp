// SPDX-License-Identifier: Apache-2.0
// Matrix-scan and keymap logic ported from the M5Stack Cardputer keyboard driver at
// https://github.com/m5stack/M5Cardputer-UserDemo/tree/main/main/hal/keyboard (MIT licensed).
#include <drivers/cardputer_keyboard.h>
#include <m5stack_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <lvgl.h>

#include <cstdlib>

static constexpr const char* TAG = "CardputerKeyboard";
#define GET_CONFIG(device) (static_cast<const M5stackCardputerKeyboardConfig*>((device)->config))

static constexpr int CARDPUTER_OUTPUT_COUNT = 3;
static constexpr int CARDPUTER_INPUT_COUNT = 7;
static constexpr int CARDPUTER_ROWS = 4;
static constexpr int CARDPUTER_COLS = 14;
// Worst case per scan: the previously active key releases and a different key is now active,
// i.e. one release event and one press event.
static constexpr int CARDPUTER_PENDING_CAPACITY = 2;

enum CardputerKeyRole {
    CARDPUTER_KEY_CHAR,
    CARDPUTER_KEY_TAB,
    CARDPUTER_KEY_FN,
    CARDPUTER_KEY_SHIFT,
    CARDPUTER_KEY_CTRL,
    CARDPUTER_KEY_OPT,
    CARDPUTER_KEY_ALT,
    CARDPUTER_KEY_DEL,
    CARDPUTER_KEY_ENTER,
    CARDPUTER_KEY_SPACE,
};

struct CardputerKeyDef {
    CardputerKeyRole role;
    // Only meaningful when role == CARDPUTER_KEY_CHAR.
    char normal;
    char shifted;
};

#define K(normal, shifted) { CARDPUTER_KEY_CHAR, normal, shifted }

// [row][col], matching the physical key grid. Modifier/action cells carry a role instead of
// a character; their normal/shifted fields are unused.
static const CardputerKeyDef cardputer_key_map[CARDPUTER_ROWS][CARDPUTER_COLS] = {
    { K('`', '~'), K('1', '!'), K('2', '@'), K('3', '#'), K('4', '$'), K('5', '%'), K('6', '^'),
      K('7', '&'), K('8', '*'), K('9', '('), K('0', ')'), K('-', '_'), K('=', '+'), { CARDPUTER_KEY_DEL, 0, 0 } },
    { { CARDPUTER_KEY_TAB, 0, 0 }, K('q', 'Q'), K('w', 'W'), K('e', 'E'), K('r', 'R'), K('t', 'T'), K('y', 'Y'),
      K('u', 'U'), K('i', 'I'), K('o', 'O'), K('p', 'P'), K('[', '{'), K(']', '}'), K('\\', '|') },
    { { CARDPUTER_KEY_FN, 0, 0 }, { CARDPUTER_KEY_SHIFT, 0, 0 }, K('a', 'A'), K('s', 'S'), K('d', 'D'), K('f', 'F'), K('g', 'G'),
      K('h', 'H'), K('j', 'J'), K('k', 'K'), K('l', 'L'), K(';', ':'), K('\'', '"'), { CARDPUTER_KEY_ENTER, 0, 0 } },
    { { CARDPUTER_KEY_CTRL, 0, 0 }, { CARDPUTER_KEY_OPT, 0, 0 }, { CARDPUTER_KEY_ALT, 0, 0 }, K('z', 'Z'), K('x', 'X'), K('c', 'C'), K('v', 'V'),
      K('b', 'B'), K('n', 'N'), K('m', 'M'), K(',', '<'), K('.', '>'), K('/', '?'), { CARDPUTER_KEY_SPACE, 0, 0 } },
};

#undef K

struct CardputerKeyboardPendingEvent {
    uint32_t key;
    bool pressed;
};

struct CardputerKeyboardInternal {
    GpioDescriptor* output_descriptors[CARDPUTER_OUTPUT_COUNT];
    GpioDescriptor* input_descriptors[CARDPUTER_INPUT_COUNT];
    // 0 when no actionable key is currently held; otherwise the LVGL key code last reported
    // via read_key(). Only ever one actionable key at a time (matches original hardware driver:
    // modifier keys are consumed internally, and only the first non-modifier key found in a
    // scan is reported).
    uint32_t active_key;
    CardputerKeyboardPendingEvent pending[CARDPUTER_PENDING_CAPACITY];
    uint8_t pending_head;
    uint8_t pending_count;
};

static void push_pending(CardputerKeyboardInternal* internal, uint32_t key, bool pressed) {
    if (internal->pending_count >= CARDPUTER_PENDING_CAPACITY) {
        LOG_W(TAG, "Pending event queue full, dropping event");
        return;
    }
    uint8_t tail = (internal->pending_head + internal->pending_count) % CARDPUTER_PENDING_CAPACITY;
    internal->pending[tail] = { .key = key, .pressed = pressed };
    internal->pending_count++;
}

static bool pop_pending(CardputerKeyboardInternal* internal, CardputerKeyboardPendingEvent* out_event) {
    if (internal->pending_count == 0) {
        return false;
    }
    *out_event = internal->pending[internal->pending_head];
    internal->pending_head = (internal->pending_head + 1) % CARDPUTER_PENDING_CAPACITY;
    internal->pending_count--;
    return true;
}

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    if (config->pins_output_count != CARDPUTER_OUTPUT_COUNT || config->pins_input_count != CARDPUTER_INPUT_COUNT) {
        LOG_E(TAG, "Expected %d output pins and %d input pins", CARDPUTER_OUTPUT_COUNT, CARDPUTER_INPUT_COUNT);
        return ERROR_INVALID_ARGUMENT;
    }

    auto* internal = static_cast<CardputerKeyboardInternal*>(malloc(sizeof(CardputerKeyboardInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    *internal = {};

    for (int i = 0; i < CARDPUTER_OUTPUT_COUNT; i++) {
        const auto& pin = config->pins_output[i];
        auto* descriptor = gpio_descriptor_acquire(pin.gpio_controller, pin.pin, GPIO_OWNER_GPIO);
        if (descriptor != nullptr && gpio_descriptor_set_flags(descriptor, pin.flags | GPIO_FLAG_DIRECTION_OUTPUT) != ERROR_NONE) {
            gpio_descriptor_release(descriptor);
            descriptor = nullptr;
        }
        if (descriptor == nullptr) {
            LOG_E(TAG, "Failed to configure output pin %d", i);
            for (int j = 0; j < i; j++) {
                gpio_descriptor_release(internal->output_descriptors[j]);
            }
            free(internal);
            return ERROR_RESOURCE;
        }
        gpio_descriptor_set_level(descriptor, false);
        internal->output_descriptors[i] = descriptor;
    }

    for (int i = 0; i < CARDPUTER_INPUT_COUNT; i++) {
        const auto& pin = config->pins_input[i];
        // Rows float high and are pulled low through a pressed key by the active output line,
        // so an internal pull-up is required regardless of what the devicetree pin flags say.
        auto* descriptor = gpio_descriptor_acquire(pin.gpio_controller, pin.pin, GPIO_OWNER_GPIO);
        if (descriptor == nullptr || gpio_descriptor_set_flags(descriptor, pin.flags | GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_PULL_UP) != ERROR_NONE) {
            LOG_E(TAG, "Failed to configure input pin %d", i);
            for (int j = 0; j < CARDPUTER_OUTPUT_COUNT; j++) {
                gpio_descriptor_release(internal->output_descriptors[j]);
            }
            for (int j = 0; j < i; j++) {
                gpio_descriptor_release(internal->input_descriptors[j]);
            }
            free(internal);
            return ERROR_RESOURCE;
        }
        internal->input_descriptors[i] = descriptor;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<CardputerKeyboardInternal*>(device_get_driver_data(device));

    for (auto* descriptor : internal->output_descriptors) {
        gpio_descriptor_release(descriptor);
    }
    for (auto* descriptor : internal->input_descriptors) {
        gpio_descriptor_release(descriptor);
    }

    free(internal);
    return ERROR_NONE;
}

// endregion

// region KeyboardApi

static void set_output(CardputerKeyboardInternal* internal, uint8_t value) {
    for (int i = 0; i < CARDPUTER_OUTPUT_COUNT; i++) {
        gpio_descriptor_set_level(internal->output_descriptors[i], ((value >> i) & 0x01) != 0);
    }
}

static uint8_t read_input(CardputerKeyboardInternal* internal) {
    uint8_t mask = 0;
    for (int j = 0; j < CARDPUTER_INPUT_COUNT; j++) {
        bool high = true;
        gpio_descriptor_get_level(internal->input_descriptors[j], &high);
        if (!high) { // active-low: LOW means pressed
            mask |= (1 << j);
        }
    }
    return mask;
}

// Scans the full matrix and resolves it to a single LVGL key code (0 if none), applying the
// same priority as the original driver: enter > space > backspace > first regular character
// found in scan order, with fn changing the interpretation of backspace/enter/punctuation.
// Modifier keys (fn/shift/ctrl/opt/alt/tab) are never reported themselves.
static uint32_t scan_key(CardputerKeyboardInternal* internal) {
    bool fn = false, shift = false, ctrl = false;
    bool del_flag = false, enter_flag = false, space_flag = false;
    bool has_regular = false;
    char regular_normal = 0, regular_shifted = 0;

    // 8 binary-encoded column-select codes x 7 row inputs = 4 rows x 14 columns.
    for (uint8_t i = 0; i < 8; i++) {
        set_output(internal, i);
        uint8_t input_mask = read_input(internal);
        if (input_mask == 0) {
            continue;
        }

        for (int j = 0; j < CARDPUTER_INPUT_COUNT; j++) {
            if ((input_mask & (1 << j)) == 0) {
                continue;
            }

            int row = 3 - (i % 4);
            int col = (i >= 4) ? (2 * j) : (2 * j + 1);
            const auto& def = cardputer_key_map[row][col];

            switch (def.role) {
                case CARDPUTER_KEY_TAB:
                case CARDPUTER_KEY_OPT:
                case CARDPUTER_KEY_ALT:
                    break; // consumed, never affects output
                case CARDPUTER_KEY_FN:
                    fn = true;
                    break;
                case CARDPUTER_KEY_SHIFT:
                    shift = true;
                    break;
                case CARDPUTER_KEY_CTRL:
                    ctrl = true;
                    break;
                case CARDPUTER_KEY_DEL:
                    del_flag = true;
                    break;
                case CARDPUTER_KEY_ENTER:
                    enter_flag = true;
                    break;
                case CARDPUTER_KEY_SPACE:
                    space_flag = true;
                    break;
                case CARDPUTER_KEY_CHAR:
                    if (!has_regular) {
                        has_regular = true;
                        regular_normal = def.normal;
                        regular_shifted = def.shifted;
                    }
                    break;
            }
        }
    }

    char resolved_char = has_regular ? ((ctrl || shift) ? regular_shifted : regular_normal) : 0;

    if (!fn) {
        if (enter_flag) return LV_KEY_ENTER;
        if (space_flag) return (uint32_t)' ';
        if (del_flag) return LV_KEY_BACKSPACE;
        if (has_regular) return (uint32_t)(uint8_t)resolved_char;
        return 0;
    }

    // fn combos: forward-delete, enter, and group navigation (using PREV/NEXT rather than
    // UP/DOWN so widgets like lv_switch that toggle on arrow keys aren't affected).
    if (del_flag) return LV_KEY_DEL;
    if (enter_flag) return LV_KEY_ENTER;
    if (has_regular) {
        switch (resolved_char) {
            case '`': return LV_KEY_ESC;
            case ',': return LV_KEY_LEFT;
            case '/': return LV_KEY_RIGHT;
            case ';': return LV_KEY_PREV;
            case '.': return LV_KEY_NEXT;
            default: return 0;
        }
    }
    return 0;
}

static error_t cardputer_keyboard_read_key(Device* device, KeyboardKeyData* data) {
    auto* internal = static_cast<CardputerKeyboardInternal*>(device_get_driver_data(device));

    uint32_t new_key = scan_key(internal);
    if (new_key != internal->active_key) {
        if (internal->active_key != 0) {
            push_pending(internal, internal->active_key, false);
        }
        if (new_key != 0) {
            push_pending(internal, new_key, true);
        }
        internal->active_key = new_key;
    }

    CardputerKeyboardPendingEvent event;
    if (pop_pending(internal, &event)) {
        data->key = event.key;
        data->pressed = event.pressed;
        data->continue_reading = internal->pending_count > 0;
    } else {
        data->key = 0;
        data->pressed = false;
        data->continue_reading = false;
    }

    return ERROR_NONE;
}

// endregion

static const KeyboardApi cardputer_keyboard_api = {
    .read_key = cardputer_keyboard_read_key,
};

Driver cardputer_keyboard_driver = {
    .name = "cardputer_keyboard",
    .compatible = (const char*[]) { "m5stack,cardputer-keyboard", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &cardputer_keyboard_api,
    .device_type = &KEYBOARD_TYPE,
    .owner = &m5stack_module,
    .internal = nullptr
};
