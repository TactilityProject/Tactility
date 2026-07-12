// SPDX-License-Identifier: Apache-2.0
// TCA8418 register protocol and Cardputer Adv row/col remap ported from
// Devices/m5stack-cardputer-adv/source/devices/CardputerKeyboard.cpp and
// Drivers/TCA8418/Source/Tca8418.cpp (itself ported from
// https://github.com/adafruit/Adafruit_TCA8418, MIT licensed).
#include <drivers/cardputer_adv_keyboard.h>
#include <m5stack_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <lvgl.h>

#include <cstdlib>

#define TAG "CardputerAdvKeyboard"
#define GET_CONFIG(device) (static_cast<const M5stackCardputerAdvKeyboardConfig*>((device)->config))

static constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(100);

// Fixed by the board's wiring (see Tca8418::init(7, 8) in the pre-kernel driver).
#define TCA8418_ROWS 7
#define TCA8418_COLS 8

#define TCA8418_REG_CFG 0x01U
#define TCA8418_REG_KEY_EVENT_A 0x04U
#define TCA8418_REG_KP_GPIO1 0x1DU
#define TCA8418_REG_KP_GPIO2 0x1EU

// AI=1, GPI_E_CFG=0, OVR_FLOW_M=0 (overflow disabled), INT_CFG=1, *_IEN=0 except KE_IEN=1.
#define TCA8418_CFG_VALUE 0x99U

// Worst case per read_key() call: drain this many queued chip events in one go.
#define CARDPUTER_ADV_MAX_EVENTS_PER_SCAN 10
#define CARDPUTER_ADV_PENDING_CAPACITY 10
#define CARDPUTER_ADV_ACTIVE_KEY_CAPACITY 4

#define CARDPUTER_ADV_ROWS 4
#define CARDPUTER_ADV_COLS 14

// [row][col] on the 4x14 grid, matching the base Cardputer's physical layout. 0 means the cell
// emits nothing (used for the sym/shift cells themselves, and unwired cells on this board).
static const uint32_t cardputer_adv_keymap_lc[CARDPUTER_ADV_ROWS][CARDPUTER_ADV_COLS] = {
    { '`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', LV_KEY_BACKSPACE },
    { '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\' },
    { 0, 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', LV_KEY_ENTER },
    { 0, 0, 0, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', ' ' },
};

static const uint32_t cardputer_adv_keymap_uc[CARDPUTER_ADV_ROWS][CARDPUTER_ADV_COLS] = {
    { '~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', LV_KEY_DEL },
    { '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '|' },
    { 0, 0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', LV_KEY_ENTER },
    { 0, 0, 0, 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', ' ' },
};

static const uint32_t cardputer_adv_keymap_sym[CARDPUTER_ADV_ROWS][CARDPUTER_ADV_COLS] = {
    { LV_KEY_ESC, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { '\t', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, LV_KEY_PREV, 0, LV_KEY_ENTER },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, LV_KEY_LEFT, LV_KEY_NEXT, LV_KEY_RIGHT, 0 },
};

struct CardputerAdvActiveKey {
    bool in_use;
    uint8_t row;
    uint8_t col;
    uint32_t key;
};

struct CardputerAdvPendingEvent {
    uint32_t key;
    bool pressed;
};

struct CardputerAdvKeyboardInternal {
    bool sym_pressed;
    bool shift_pressed;
    bool caps_lock;
    // Only allows one caps-lock toggle per sym+shift co-press, requiring both to be released
    // before it can be toggled again.
    bool caps_lock_armed;

    CardputerAdvActiveKey active_keys[CARDPUTER_ADV_ACTIVE_KEY_CAPACITY];

    CardputerAdvPendingEvent pending[CARDPUTER_ADV_PENDING_CAPACITY];
    uint8_t pending_head;
    uint8_t pending_count;
};

static void push_pending(CardputerAdvKeyboardInternal* internal, uint32_t key, bool pressed) {
    if (internal->pending_count >= CARDPUTER_ADV_PENDING_CAPACITY) {
        LOG_W(TAG, "Pending event queue full, dropping event");
        return;
    }
    uint8_t tail = (internal->pending_head + internal->pending_count) % CARDPUTER_ADV_PENDING_CAPACITY;
    internal->pending[tail] = { .key = key, .pressed = pressed };
    internal->pending_count++;
}

static bool pop_pending(CardputerAdvKeyboardInternal* internal, CardputerAdvPendingEvent* out_event) {
    if (internal->pending_count == 0) {
        return false;
    }
    *out_event = internal->pending[internal->pending_head];
    internal->pending_head = (internal->pending_head + 1) % CARDPUTER_ADV_PENDING_CAPACITY;
    internal->pending_count--;
    return true;
}

// region Driver lifecycle

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    if (i2c_controller_has_device_at_address(parent, config->address, I2C_TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "No device found on I2C bus at address 0x%02X", config->address);
        return ERROR_RESOURCE;
    }

    auto* internal = static_cast<CardputerAdvKeyboardInternal*>(malloc(sizeof(CardputerAdvKeyboardInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    *internal = {};
    internal->caps_lock_armed = true;

    // KP_GPIO1/2: mark all TCA8418_ROWS rows and TCA8418_COLS columns as keypad matrix pins.
    uint8_t row_mask = (1U << TCA8418_ROWS) - 1;
    uint8_t col_mask = (1U << TCA8418_COLS) - 1;
    bool ok =
        i2c_controller_register8_set(parent, config->address, TCA8418_REG_KP_GPIO1, row_mask, I2C_TIMEOUT) == ERROR_NONE &&
        i2c_controller_register8_set(parent, config->address, TCA8418_REG_KP_GPIO2, col_mask, I2C_TIMEOUT) == ERROR_NONE &&
        i2c_controller_register8_set(parent, config->address, TCA8418_REG_CFG, TCA8418_CFG_VALUE, I2C_TIMEOUT) == ERROR_NONE;
    if (!ok) {
        LOG_E(TAG, "Failed to configure TCA8418");
        free(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<CardputerAdvKeyboardInternal*>(device_get_driver_data(device));
    free(internal);
    return ERROR_NONE;
}

// endregion

// region KeyboardApi

// Wiring is a 7x8 matrix, but the keymap tables are laid out as a 4x14 grid (matching the base
// Cardputer's physical key positions).
static void remap_to_grid(uint8_t wiring_row, uint8_t wiring_col, uint8_t* grid_row, uint8_t* grid_col) {
    uint8_t col = wiring_row * 2;
    if (wiring_col > 3) {
        col++;
    }
    *grid_row = (wiring_col + 4) % 4;
    *grid_col = col;
}

static uint32_t resolve_key(const CardputerAdvKeyboardInternal* internal, uint8_t row, uint8_t col) {
    if (internal->sym_pressed) {
        return cardputer_adv_keymap_sym[row][col];
    }
    if (internal->shift_pressed || internal->caps_lock) {
        return cardputer_adv_keymap_uc[row][col];
    }
    return cardputer_adv_keymap_lc[row][col];
}

static void handle_key_event(CardputerAdvKeyboardInternal* internal, uint8_t row, uint8_t col, bool pressed) {
    // Sym and shift are modifiers: tracked as state, never emitted as their own key events.
    if (row == 2 && col == 0) {
        internal->sym_pressed = pressed;
    } else if (row == 2 && col == 1) {
        internal->shift_pressed = pressed;
    } else if (pressed) {
        if (internal->sym_pressed && internal->shift_pressed && internal->caps_lock_armed) {
            internal->caps_lock = !internal->caps_lock;
            internal->caps_lock_armed = false;
        }

        uint32_t key = resolve_key(internal, row, col);
        if (key != 0) {
            for (auto& active_key : internal->active_keys) {
                if (!active_key.in_use) {
                    active_key = { .in_use = true, .row = row, .col = col, .key = key };
                    push_pending(internal, key, true);
                    break;
                }
            }
        }
    } else {
        // Release: report the key that was actually pressed, even if the active modifier layer
        // changed while it was held.
        for (auto& active_key : internal->active_keys) {
            if (active_key.in_use && active_key.row == row && active_key.col == col) {
                push_pending(internal, active_key.key, false);
                active_key.in_use = false;
                break;
            }
        }
    }

    if (!internal->sym_pressed && !internal->shift_pressed) {
        internal->caps_lock_armed = true;
    }
}

static error_t cardputer_adv_keyboard_read_key(Device* device, KeyboardKeyData* data) {
    auto* parent = device_get_parent(device);
    const auto* config = GET_CONFIG(device);
    auto* internal = static_cast<CardputerAdvKeyboardInternal*>(device_get_driver_data(device));

    for (int i = 0; i < CARDPUTER_ADV_MAX_EVENTS_PER_SCAN; i++) {
        uint8_t key_event = 0;
        if (i2c_controller_register8_get(parent, config->address, TCA8418_REG_KEY_EVENT_A, &key_event, I2C_TIMEOUT) != ERROR_NONE) {
            break;
        }
        if (key_event == 0) {
            break; // Chip event FIFO is empty.
        }

        bool pressed = (key_event & 0x80) != 0;
        uint8_t code = (key_event & 0x7F) - 1;
        uint8_t wiring_row = code / 10;
        uint8_t wiring_col = code % 10;

        uint8_t grid_row, grid_col;
        remap_to_grid(wiring_row, wiring_col, &grid_row, &grid_col);
        handle_key_event(internal, grid_row, grid_col, pressed);
    }

    CardputerAdvPendingEvent event;
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

static const KeyboardApi cardputer_adv_keyboard_api = {
    .read_key = cardputer_adv_keyboard_read_key,
};

Driver cardputer_adv_keyboard_driver = {
    .name = "cardputer_adv_keyboard",
    .compatible = (const char*[]) { "m5stack,cardputer-adv-keyboard", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &cardputer_adv_keyboard_api,
    .device_type = &KEYBOARD_TYPE,
    .owner = &m5stack_module,
    .internal = nullptr
};
