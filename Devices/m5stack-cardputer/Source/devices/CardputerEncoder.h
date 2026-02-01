#pragma once

#include <Tactility/hal/encoder/EncoderDevice.h>

#include "keyboard/keyboard.h"

/**
 * Wrapper around the keyboard that uses the following buttons to simulate an encoder:
 * - Up
 * - Down
 * - ok (fn + enter)
 */
class CardputerEncoder final : public tt::hal::encoder::EncoderDevice {
    KEYBOARD::Keyboard keyboard;
    int lastKeyNum = 0;
    lv_indev_t* lvglDevice = nullptr;

    static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);

public:

    std::string getName() const override { return "Cardputer Encoder"; }
    std::string getDescription() const override { return "Cardputer keyboard up/down acting as encoder"; }

    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;

    lv_indev_t* getLvglIndev() override { return lvglDevice; }
};
