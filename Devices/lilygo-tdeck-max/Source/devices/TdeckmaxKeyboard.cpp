#include "TdeckmaxKeyboard.h"

#include <tactility/log.h>

#include <driver/i2c.h>
#include <driver/gpio.h>

constexpr auto* TAG = "TdeckmaxKeyboard";

// Keyboard backlight (LED_PWM), GPIO42 on the T-Deck Max.
constexpr auto BACKLIGHT = GPIO_NUM_42;

constexpr auto KB_ROWS = 4;
constexpr auto KB_COLS = 10;

// Keymaps are written in the vendor's row/column orientation
// (Xinyuan-LilyGO/T-Deck-MAX examples/factory/peri_keypad.cpp). The TCA8418
// columns are wired in reverse, so the vendor reads keymap[row][9 - col]. We do
// the same reversal in processKeyboard(), which keeps these tables a direct,
// verifiable copy of the vendor layout.
//
// Modifier keys (ALT and SYM) are blanked here ('\0') and handled by position:
//   ALT -> shift/uppercase, at vendor column 0 of row 2
//   SYM -> symbol layer,     at vendor column 8 of row 3

// Lowercase (base) layer
static constexpr char keymap_lc[KB_ROWS][KB_COLS] = {
    {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
    {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', LV_KEY_BACKSPACE},
    {'\0', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '$', LV_KEY_ENTER},
    {'\0', '\0', '\0', '\0', '\0', LV_KEY_PREV, '0', ' ', '\0', LV_KEY_NEXT}
};

// Uppercase layer (ALT held or caps toggled)
static constexpr char keymap_uc[KB_ROWS][KB_COLS] = {
    {'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P'},
    {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', LV_KEY_BACKSPACE},
    {'\0', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '$', LV_KEY_ENTER},
    {'\0', '\0', '\0', '\0', '\0', LV_KEY_PREV, '0', ' ', '\0', LV_KEY_NEXT}
};

// Symbol layer (SYM held). The T-Deck Max silkscreen for the symbol layer is
// not documented in the vendor sources, so these are sensible defaults; adjust
// to match the printed keys once verified on hardware.
static constexpr char keymap_sy[KB_ROWS][KB_COLS] = {
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
    {'@', '#', '+', '-', '*', '/', '(', ')', '_', LV_KEY_BACKSPACE},
    {'\0', '!', '?', ';', ':', '\'', '"', ',', '.', LV_KEY_ENTER},
    {'\0', '\0', '\0', '\0', '\0', LV_KEY_PREV, '0', ' ', '\0', LV_KEY_NEXT}
};

void TdeckmaxKeyboard::readCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto keyboard = static_cast<TdeckmaxKeyboard*>(lv_indev_get_user_data(indev));

    // Emit the release edge for the previous key first: LVGL's keypad handling
    // only delivers a key on a RELEASED->PRESSED transition, so two different
    // keys reported PRESSED back-to-back would swallow the second one.
    if (keyboard->lastKeyNeedsRelease) {
        keyboard->lastKeyNeedsRelease = false;
        data->key = keyboard->lastKey;
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = uxQueueMessagesWaiting(keyboard->queue) > 0;
        return;
    }

    char keypress = 0;
    if (xQueueReceive(keyboard->queue, &keypress, 0) == pdPASS) {
        keyboard->lastKey = static_cast<uint32_t>(keypress);
        keyboard->lastKeyNeedsRelease = true;
        data->key = keyboard->lastKey;
        data->state = LV_INDEV_STATE_PRESSED;
        // Drain the whole queue in this read cycle so a typing burst lands in
        // one render pass (and thus a single e-paper refresh).
        data->continue_reading = true;
    } else {
        data->key = 0;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void TdeckmaxKeyboard::processKeyboard() {
    static bool shift_pressed = false;
    static bool sym_pressed = false;
    static bool cap_toggle = false;
    static bool cap_toggle_armed = true;
    bool anykey_pressed = false;

    // Each update() pops one event from the TCA8418's FIFO. Drain it fully per
    // poll (bounded, in case of a misbehaving bus) so a fast typing burst isn't
    // throttled to one event per poll period. Handling each event as a discrete
    // press/release edge also means a key held across another key's press (fast
    // typing rollover) no longer re-sends its character.
    for (int drained = 0; drained < 16 && keypad->update(); drained++) {
        if (keypad->released_key_count > 0) {
            // Release event: only modifier state cares about releases.
            auto row = keypad->released_list[0].row;
            auto vcol = (KB_COLS - 1) - keypad->released_list[0].col;

            if ((row == 2) && (vcol == 0)) {
                shift_pressed = false; // ALT key
            }
            if ((row == 3) && (vcol == 8)) {
                sym_pressed = false; // SYM key
            }
        } else if (keypad->pressed_key_count > 0) {
            // Press event: the key that caused it is the newest list entry.
            auto row = keypad->pressed_list[keypad->pressed_key_count - 1].row;
            auto vcol = (KB_COLS - 1) - keypad->pressed_list[keypad->pressed_key_count - 1].col;
            anykey_pressed = true;

            if ((row == 2) && (vcol == 0)) {
                shift_pressed = true; // ALT key
            } else if ((row == 3) && (vcol == 8)) {
                sym_pressed = true; // SYM key
            } else {
                char chr;
                if (sym_pressed) {
                    chr = keymap_sy[row][vcol];
                } else if (shift_pressed || cap_toggle) {
                    chr = keymap_uc[row][vcol];
                } else {
                    chr = keymap_lc[row][vcol];
                }

                if (chr != '\0') xQueueSend(queue, &chr, 50 / portTICK_PERIOD_MS);
            }

            if ((sym_pressed && shift_pressed) && cap_toggle_armed) {
                cap_toggle = !cap_toggle;
                cap_toggle_armed = false;
            }
        }

        if ((!sym_pressed && !shift_pressed) && !cap_toggle_armed) {
            cap_toggle_armed = true;
        }
    }

    if (anykey_pressed) {
        makeBacklightImpulse();
    }
}

bool TdeckmaxKeyboard::startLvgl(lv_display_t* display) {
    backlightOkay = initBacklight(BACKLIGHT, 30000, LEDC_TIMER_0, LEDC_CHANNEL_1);
    keypad->init(KB_ROWS, KB_COLS);

    assert(inputTimer == nullptr);
    inputTimer = std::make_unique<tt::Timer>(tt::Timer::Type::Periodic, tt::kernel::millisToTicks(20), [this] {
        processKeyboard();
    });

    assert(backlightImpulseTimer == nullptr);
    backlightImpulseTimer = std::make_unique<tt::Timer>(tt::Timer::Type::Periodic, tt::kernel::millisToTicks(50), [this] {
        processBacklightImpulse();
    });

    kbHandle = lv_indev_create();
    lv_indev_set_type(kbHandle, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(kbHandle, &readCallback);
    lv_indev_set_display(kbHandle, display);
    lv_indev_set_user_data(kbHandle, this);

    inputTimer->start();
    backlightImpulseTimer->start();

    return true;
}

bool TdeckmaxKeyboard::stopLvgl() {
    assert(inputTimer);
    inputTimer->stop();
    inputTimer = nullptr;

    assert(backlightImpulseTimer);
    backlightImpulseTimer->stop();
    backlightImpulseTimer = nullptr;

    lv_indev_delete(kbHandle);
    kbHandle = nullptr;
    return true;
}

bool TdeckmaxKeyboard::isAttached() const {
    return i2c_controller_has_device_at_address(keypad->getController(), keypad->getAddress(), 100) == ERROR_NONE;
}

bool TdeckmaxKeyboard::initBacklight(gpio_num_t pin, uint32_t frequencyHz, ledc_timer_t timer, ledc_channel_t channel) {
    backlightPin = pin;
    backlightTimer = timer;
    backlightChannel = channel;

    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = backlightTimer,
        .freq_hz = frequencyHz,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false
    };

    if (ledc_timer_config(&ledc_timer) != ESP_OK) {
        LOG_E(TAG, "Backlight timer config failed");
        return false;
    }

    ledc_channel_config_t ledc_channel = {
        .gpio_num = backlightPin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = backlightChannel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = backlightTimer,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = 0
        }
    };

    if (ledc_channel_config(&ledc_channel) != ESP_OK) {
        LOG_E(TAG, "Backlight channel config failed");
    }

    return true;
}

bool TdeckmaxKeyboard::setBacklightDuty(uint8_t duty) {
    if (!backlightOkay) {
        LOG_E(TAG, "Backlight not ready");
        return false;
    }
    return (ledc_set_duty(LEDC_LOW_SPEED_MODE, backlightChannel, duty) == ESP_OK) &&
        (ledc_update_duty(LEDC_LOW_SPEED_MODE, backlightChannel) == ESP_OK);
}

void TdeckmaxKeyboard::makeBacklightImpulse() {
    backlightImpulseDuty = 255;
    setBacklightDuty(backlightImpulseDuty);
}

void TdeckmaxKeyboard::processBacklightImpulse() {
    if (backlightImpulseDuty > 0) {
        backlightImpulseDuty--;
        setBacklightDuty(backlightImpulseDuty);
    }
}
