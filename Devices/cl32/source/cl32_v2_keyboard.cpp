// SPDX-License-Identifier: Apache-2.0
#include "cl32_v2_keyboard.h"

#include <drivers/tca8418.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/log.h>

constexpr auto* TAG = "cl32-keyboard";

// Keymaps and matrix layout, copied from the original cl32.dts "keyboard" node (removed once
// keyboard creation became conditional on revision-2 hardware detection).
static constexpr uint32_t KEYMAP_LC[80] = {
    37, 49, 50, 51, 52, 53, 54, 55, 56, 0,   // % 1 2 3 4 5 6 7 8
    57, 48, 8, 91, 93, 43, 34, 39, 27, 0,    // 9 0 BKSP [ ] + " ' EXIT
    9, 113, 119, 101, 114, 116, 121, 117, 105, 0, // TAB q w e r t y u i
    111, 112, 13, 40, 41, 45, 59, 58, 3, 0,  // o p ENTER ( ) - ; : STOP
    0, 97, 115, 100, 102, 103, 104, 106, 107, 0,  //   a s d f g h j k
    108, 17, 35, 123, 125, 42, 44, 46, 2, 0, // l UP # { } * , . MENU
    122, 120, 99, 118, 98, 32, 32, 110, 109, 0,   // z x c v b   n m
    CODEPOINT_ARROW_LEFT, CODEPOINT_ARROW_DOWN, CODEPOINT_ARROW_RIGHT, 60, 62, 47, 92, 61, 13, 0,   // LEFT DOWN RIGHT < > / \ = RUN
};
static constexpr uint32_t KEYMAP_UC[80] = {
    37, 49, 50, 51, 52, 53, 54, 55, 56, 0,   // % 1 2 3 4 5 6 7 8
    57, 48, 8, 91, 93, 43, 34, 39, 27, 0,    // 9 0 BKSP [ ] + " ' EXIT
    9, 81, 87, 69, 82, 84, 89, 85, 73, 0,    // TAB Q W E R T Y U I
    79, 80, 13, 40, 41, 45, 59, 58, 3, 0,    // O P ENTER ( ) - ; : STOP
    0, 65, 83, 68, 70, 71, 72, 74, 75, 0,    //   A S D F G H J K
    76, 17, 35, 123, 125, 42, 44, 46, 2, 0,  // L UP # { } * , . MENU
    90, 88, 67, 86, 66, 32, 32, 78, 77, 0,   // Z X C V B   N M
    CODEPOINT_ARROW_LEFT, CODEPOINT_ARROW_DOWN, CODEPOINT_ARROW_RIGHT, 60, 62, 47, 92, 61, 13, 0,   // LEFT DOWN RIGHT < > / \ = RUN
};
static constexpr uint32_t KEYMAP_SY[80] = {
    37, 49, 50, 51, 52, 53, 54, 55, 56, 0,   // % 1 2 3 4 5 6 7 8
    57, 48, 8, 91, 93, 43, 34, 39, 27, 0,    // 9 0 BKSP [ ] + " ' EXIT
    9, 113, 119, 101, 114, 116, 121, 117, 105, 0, // TAB q w e r t y u i
    111, 112, 13, 40, 41, 45, 59, 58, 3, 0,  // o p ENTER ( ) - ; : STOP
    0, 97, 115, 100, 102, 103, 104, 106, 107, 0,  //   a s d f g h j k
    108, 17, 35, 123, 125, 42, 44, 46, 2, 0, // l UP # { } * , . MENU
    122, 120, 99, 118, 98, 32, 32, 110, 109, 0,   // z x c v b   n m
    CODEPOINT_ARROW_LEFT, CODEPOINT_ARROW_DOWN, CODEPOINT_ARROW_RIGHT, 60, 62, 47, 92, 61, 13, 0,   // LEFT DOWN RIGHT < > / \ = RUN
};

static Tca8418Config cl32_keyboard_config {};
static Device cl32_keyboard_device {};

void cl32_create_keyboard(Device* i2c0) {
    cl32_keyboard_config = Tca8418Config {
        .address = 0x34,
        .rows = 8,
        .columns = 10,
        .reverse_columns = false,
        .keymap_lc = KEYMAP_LC,
        .keymap_lc_length = sizeof(KEYMAP_LC) / sizeof(KEYMAP_LC[0]),
        .keymap_uc = KEYMAP_UC,
        .keymap_uc_length = sizeof(KEYMAP_UC) / sizeof(KEYMAP_UC[0]),
        .keymap_sy = KEYMAP_SY,
        .keymap_sy_length = sizeof(KEYMAP_SY) / sizeof(KEYMAP_SY[0]),
        .shift_row = 4,
        .shift_col = 0,
        .sym_row = 5,
        .sym_col = 8,
        .pin_reset = GPIO_PIN_SPEC_NONE,
    };

    cl32_keyboard_device = Device {
        .address = 0,
        .name = "keyboard",
        .config = &cl32_keyboard_config,
        .parent = nullptr,
        .flags = 0,
        .internal = nullptr,
    };

    error_t error = device_construct(&cl32_keyboard_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to construct keyboard: %s", error_to_string(error));
        return;
    }

    device_set_parent(&cl32_keyboard_device, i2c0);

    Driver* driver = driver_find_compatible("ti,tca8418");
    if (driver == nullptr) {
        LOG_E(TAG, "No driver registered for ti,tca8418");
        device_destruct(&cl32_keyboard_device);
        return;
    }
    device_set_driver(&cl32_keyboard_device, driver);

    error = device_add(&cl32_keyboard_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to add keyboard: %s", error_to_string(error));
        device_destruct(&cl32_keyboard_device);
        return;
    }

    error = device_start(&cl32_keyboard_device);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to start keyboard: %s", error_to_string(error));
        device_remove(&cl32_keyboard_device);
        device_destruct(&cl32_keyboard_device);
        return;
    }
}
