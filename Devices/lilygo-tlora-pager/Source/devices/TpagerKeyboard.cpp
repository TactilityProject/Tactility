#include "TpagerKeyboard.h"

#include <Tactility/Logger.h>

#include <driver/i2c.h>
#include <driver/gpio.h>

static const auto LOGGER = tt::Logger("TpagerKeyboard");

constexpr auto BACKLIGHT = GPIO_NUM_46;

constexpr auto KB_ROWS = 4;
constexpr auto KB_COLS = 10;

// Lowercase Keymap
static constexpr char keymap_lc[KB_ROWS][KB_COLS] = {
    {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
    {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', LV_KEY_ENTER},
    {'\0', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '\0', LV_KEY_BACKSPACE},
    {' ', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'}
};

// Uppercase Keymap
static constexpr char keymap_uc[KB_ROWS][KB_COLS] = {
    {'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P'},
    {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', LV_KEY_ENTER},
    {'\0', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '\0', LV_KEY_BACKSPACE},
    {' ', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'}
};

// Symbol Keymap
static constexpr char keymap_sy[KB_ROWS][KB_COLS] = {
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
    {'.', '/', '+', '-', '=', ':', '\'', '"', '@', '\t'},
    {'\0', '_', '$', ';', '?', '!', ',', '.', '\0', LV_KEY_BACKSPACE},
    {' ', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'}
};

void TpagerKeyboard::readCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto keyboard = static_cast<TpagerKeyboard*>(lv_indev_get_user_data(indev));
    char keypress = 0;

    if (xQueueReceive(keyboard->queue, &keypress, 0) == pdPASS) {
        data->key = keypress;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->key = 0;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void TpagerKeyboard::processKeyboard() {
    static bool shift_pressed = false;
    static bool sym_pressed = false;
    static bool cap_toggle = false;
    static bool cap_toggle_armed = true;
    bool anykey_pressed = false;

    if (keypad->update()) {
        anykey_pressed = (keypad->pressed_key_count > 0);
        for (int i = 0; i < keypad->pressed_key_count; i++) {
            auto row = keypad->pressed_list[i].row;
            auto col = keypad->pressed_list[i].col;
            auto hold = keypad->pressed_list[i].hold_time;

            if ((row == 2) && (col == 0)) {
                sym_pressed = true;
            }
            if ((row == 2) && (col == 8)) {
                shift_pressed = true;
            }
        }

        if ((sym_pressed && shift_pressed) && cap_toggle_armed) {
            cap_toggle = !cap_toggle;
            cap_toggle_armed = false;
        }

        for (int i = 0; i < keypad->pressed_key_count; i++) {
            auto row = keypad->pressed_list[i].row;
            auto col = keypad->pressed_list[i].col;
            auto hold = keypad->pressed_list[i].hold_time;
            char chr = '\0';
            if (sym_pressed) {
                chr = keymap_sy[row][col];
            } else if (shift_pressed || cap_toggle) {
                chr = keymap_uc[row][col];
            } else {
                chr = keymap_lc[row][col];
            }

            if (chr != '\0') xQueueSend(queue, &chr, 50 / portTICK_PERIOD_MS);
        }

        for (int i = 0; i < keypad->released_key_count; i++) {
            auto row = keypad->released_list[i].row;
            auto col = keypad->released_list[i].col;

            if ((row == 2) && (col == 0)) {
                sym_pressed = false;
            }
            if ((row == 2) && (col == 8)) {
                shift_pressed = false;
            }
        }

        if ((!sym_pressed && !shift_pressed) && !cap_toggle_armed) {
            cap_toggle_armed = true;
        }

        if (anykey_pressed) {
            makeBacklightImpulse();
        }
    }
}

bool TpagerKeyboard::startLvgl(lv_display_t* display) {
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

bool TpagerKeyboard::stopLvgl() {
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

bool TpagerKeyboard::isAttached() const {
    return i2c_controller_has_device_at_address(keypad->getController(), keypad->getAddress(), 100) == ERROR_NONE;
}

bool TpagerKeyboard::initBacklight(gpio_num_t pin, uint32_t frequencyHz, ledc_timer_t timer, ledc_channel_t channel) {
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
        LOGGER.error("Backlight timer config failed");
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
        LOGGER.error("Backlight channel config failed");
    }

    return true;
}

bool TpagerKeyboard::setBacklightDuty(uint8_t duty) {
    if (!backlightOkay) {
        LOGGER.error("Backlight not ready");
        return false;
    }
    return (ledc_set_duty(LEDC_LOW_SPEED_MODE, backlightChannel, duty) == ESP_OK) &&
        (ledc_update_duty(LEDC_LOW_SPEED_MODE, backlightChannel) == ESP_OK);
}

void TpagerKeyboard::makeBacklightImpulse() {
    backlightImpulseDuty = 255;
    setBacklightDuty(backlightImpulseDuty);
}

void TpagerKeyboard::processBacklightImpulse() {
    if (backlightImpulseDuty > 64) {
        backlightImpulseDuty--;
        setBacklightDuty(backlightImpulseDuty);
    }
}
