// SPDX-License-Identifier: Apache-2.0
#include "cl32_v3_keyboard.h"

#include <drivers/tca8418.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/log.h>

constexpr auto* TAG = "cl32-v3-keyboard";

// Reference: https://github.com/CL-32/CL-32/blob/8618d991eb6d9cf291eccb8400e94f303420cece/Software/CL-32/lib/CL32/src/CL32_keyboard.cpp

// Keymaps and matrix layout, transcribed from CL-32/CL-32's CL32_keyboard.cpp CL32_keyboard::_matrix
// (same tca8418 chip/wiring as cl32_v2_keyboard.cpp, different physical key layout).
static constexpr uint32_t KEYMAP_LC[70] = {
    CODEPOINT_TAB, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 0,       // TAB q w e r t y u i
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 0,       // SHIFT a s d f g h j k
    0, 'z', 'x', 'c', ' ', ' ', 'v', 'b', 'n', 0,       // FN z x c (space)(space) v b n
    '=', '.', '0', '+', CODEPOINT_ARROW_UP, CODEPOINT_ARROW_RIGHT, CODEPOINT_ENTER, CODEPOINT_ENTER, 'm', 0,         // = . 0 + UP RIGHT ENTER ENTER m
    '3', '2', '1', '-', CODEPOINT_ARROW_LEFT, CODEPOINT_ARROW_DOWN, CODEPOINT_ESCAPE, CODEPOINT_ENTER, 'l', 0,         // 3 2 1 - LEFT DOWN ESC ENTER l
    '6', '5', '4', '*', 67, 68, 8, 'p', 'o', 0,         // 6 5 4 * FILE MENU BKSP p o
    '9', '8', '7', '/', 0, 0, 0, 0, 0, 0,               // 9 8 7 /
};
static constexpr uint32_t KEYMAP_UC[70] = {
    CODEPOINT_TAB, 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 0,
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 0,
    0, 'Z', 'X', 'C', ' ', ' ', 'V', 'B', 'N', 0,
    '=', '.', '0', '+', CODEPOINT_ARROW_UP, CODEPOINT_ARROW_RIGHT, CODEPOINT_ENTER, CODEPOINT_ENTER, 'M', 0,
    '3', '2', '1', '-', CODEPOINT_ARROW_LEFT, CODEPOINT_ARROW_DOWN, CODEPOINT_ESCAPE, CODEPOINT_ENTER, 'L', 0,
    '6', '5', '4', '*', 67, 68, 8, 'P', 'O', 0,
    '9', '8', '7', '/', 0, 0, 0, 0, 0, 0,
};
static constexpr uint32_t KEYMAP_SY[70] = {
    CODEPOINT_TAB, '!', '"', '@', 0xA3 /* GBP sign */, '$', '%', '^', '&', 0,
    0, '|', '#', ',', '?', '\'', ':', ';', '{', 0,
    0, ' ', '\\', '~', ' ', ' ', '<', '>', '[', 0,
    '=', '.', '0', '+', CODEPOINT_ARROW_UP, CODEPOINT_ARROW_RIGHT, CODEPOINT_ENTER, CODEPOINT_ENTER, ']', 0,
    '3', '2', '1', '-', CODEPOINT_ARROW_LEFT, CODEPOINT_ARROW_DOWN, CODEPOINT_ESCAPE, CODEPOINT_ENTER, '}', 0,
    '6', '5', '4', '*', 67, 68, 8, ')', '(', 0,
    '9', '8', '7', '/', 0, 0, 0, 0, 0, 0,
};

static Tca8418Config cl32_v3_keyboard_config {};
static Device cl32_v3_keyboard_device {};

void cl32_v3_create_keyboard(Device* i2c0) {
    cl32_v3_keyboard_config = Tca8418Config {
        .address = 0x34,
        .rows = 7,
        .columns = 10,
        .reverse_columns = false,
        .keymap_lc = KEYMAP_LC,
        .keymap_lc_length = sizeof(KEYMAP_LC) / sizeof(KEYMAP_LC[0]),
        .keymap_uc = KEYMAP_UC,
        .keymap_uc_length = sizeof(KEYMAP_UC) / sizeof(KEYMAP_UC[0]),
        .keymap_sy = KEYMAP_SY,
        .keymap_sy_length = sizeof(KEYMAP_SY) / sizeof(KEYMAP_SY[0]),
        .shift_row = 1,
        .shift_col = 0,
        .sym_row = 2,
        .sym_col = 0,
        .pin_reset = GPIO_PIN_SPEC_NONE,
    };

    cl32_v3_keyboard_device = Device {
        .address = 0,
        .name = "cl32-v3-keyboard",
        .config = &cl32_v3_keyboard_config,
        .parent = nullptr,
        .flags = 0,
        .internal = nullptr,
    };

    error_t error = device_construct(&cl32_v3_keyboard_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to construct keyboard: %s", error_to_string(error));
        return;
    }

    device_set_parent(&cl32_v3_keyboard_device, i2c0);

    Driver* driver = driver_find_compatible("ti,tca8418");
    if (driver == nullptr) {
        LOG_E(TAG, "No driver registered for ti,tca8418");
        device_destruct(&cl32_v3_keyboard_device);
        return;
    }
    device_set_driver(&cl32_v3_keyboard_device, driver);

    error = device_add(&cl32_v3_keyboard_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to add keyboard: %s", error_to_string(error));
        device_destruct(&cl32_v3_keyboard_device);
        return;
    }

    error = device_start(&cl32_v3_keyboard_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to start keyboard: %s", error_to_string(error));
        device_remove(&cl32_v3_keyboard_device);
        device_destruct(&cl32_v3_keyboard_device);
        return;
    }
}
