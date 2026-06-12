#include "CardputerKeyboard.h"
#include <tactility/drivers/i2c_controller.h>

constexpr auto* TAG = "CardputerKeyb";

constexpr auto BACKLIGHT = GPIO_NUM_46;

constexpr auto KB_ROWS = 14;
constexpr auto KB_COLS = 4;

// Lowercase Keymap
static constexpr char keymap_lc[KB_COLS][KB_ROWS] = {
    {'`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '_', '=', LV_KEY_BACKSPACE},
    {'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\'},
    {'\0', '\0', 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', LV_KEY_ENTER},
    {'\0', '\0', '\0', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', ' '}
};

// Uppercase Keymap
static constexpr char keymap_uc[KB_COLS][KB_ROWS] = {
    {'~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '-', '+', LV_KEY_DEL},
    {'\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '|'},
    {'\0', '\0', 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', LV_KEY_ENTER},
    {'\0', '\0', '\0', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', ' '}
};

// Symbol Keymap
static constexpr char keymap_sy[KB_COLS][KB_ROWS] = {
    {LV_KEY_ESC, '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'},
    {'\t', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'},
    {'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', LV_KEY_PREV, '\0', LV_KEY_ENTER},
    {'\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', LV_KEY_LEFT, LV_KEY_NEXT, LV_KEY_RIGHT, '\0'}
};

void CardputerKeyboard::readCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto keyboard = static_cast<CardputerKeyboard*>(lv_indev_get_user_data(indev));
    char keypress = 0;

    if (xQueueReceive(keyboard->queue, &keypress, 0) == pdPASS) {
        data->key = keypress;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->key = 0;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void CardputerKeyboard::remap(uint8_t& row, uint8_t& col) {
    // Col
    uint8_t coltemp = row * 2;
    if (col > 3) coltemp++;

    // Row
    uint8_t rowtemp = (col + 4) % 4;

    row = rowtemp;
    col = coltemp;
}

void CardputerKeyboard::processKeyboard() {
    static bool shift_pressed = false;
    static bool sym_pressed = false;
    static bool cap_toggle = false;
    static bool cap_toggle_armed = true;

    if (keypad->update()) {
        // Check if symbol or shift is pressed
        for (int i = 0; i < keypad->pressed_key_count; i++) {
            // Swap rows and columns
            uint8_t row = keypad->pressed_list[i].row;
            uint8_t column = keypad->pressed_list[i].col;
            remap(row, column);

            if ((row == 2) && (column == 0)) {
                sym_pressed = true;
            }
            if ((row == 2) && (column == 1)) {
                shift_pressed = true;
            }
        }

        // Toggle caps lock
        if ((sym_pressed && shift_pressed) && cap_toggle_armed) {
            cap_toggle = !cap_toggle;
            cap_toggle_armed = false;
        }

        // Process regular key input given the processed modifiers
        for (int i = 0; i < keypad->pressed_key_count; i++) {
            auto row = keypad->pressed_list[i].row;
            auto column = keypad->pressed_list[i].col;
            remap(row, column);
            char chr = '\0';
            if (sym_pressed) {
                chr = keymap_sy[row][column];
            } else if (shift_pressed || cap_toggle) {
                chr = keymap_uc[row][column];
            } else {
                chr = keymap_lc[row][column];
            }

            if (chr != '\0') xQueueSend(queue, &chr, 50 / portTICK_PERIOD_MS);
        }

        for (int i = 0; i < keypad->released_key_count; i++) {
            auto row = keypad->released_list[i].row;
            auto column = keypad->released_list[i].col;
            remap(row, column);

            if ((row == 2) && (column == 0)) {
                sym_pressed = false;
            }
            if ((row == 2) && (column == 1)) {
                shift_pressed = false;
            }
        }

        if ((!sym_pressed && !shift_pressed) && !cap_toggle_armed) {
            cap_toggle_armed = true;
        }
    }
}

bool CardputerKeyboard::startLvgl(lv_display_t* display) {
    keypad->init(7, 8);

    assert(inputTimer == nullptr);
    inputTimer = std::make_unique<tt::Timer>(tt::Timer::Type::Periodic, pdMS_TO_TICKS(20), [this] {
        processKeyboard();
    });

    kbHandle = lv_indev_create();
    lv_indev_set_type(kbHandle, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(kbHandle, &readCallback);
    lv_indev_set_display(kbHandle, display);
    lv_indev_set_user_data(kbHandle, this);

    inputTimer->start();

    return true;
}

bool CardputerKeyboard::stopLvgl() {
    assert(inputTimer);
    inputTimer->stop();
    inputTimer = nullptr;

    lv_indev_delete(kbHandle);
    kbHandle = nullptr;
    return true;
}

bool CardputerKeyboard::isAttached() const {
    return i2c_controller_has_device_at_address(keypad->getController(), keypad->getAddress(), 100) == ERROR_NONE;
}
