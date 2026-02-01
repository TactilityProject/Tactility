#pragma once

#include <Tactility/Thread.h>
#include <Tactility/hal/keyboard/KeyboardDevice.h>

#include "keyboard/keyboard.h"

class CardputerKeyboard : public tt::hal::keyboard::KeyboardDevice {
    KEYBOARD::Keyboard keyboard;
    int lastKeyNum = 0;
    lv_indev_t* lvglDevice = nullptr;

    static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);

public:

    std::string getName() const override { return "Cardputer Keyboard"; }
    std::string getDescription() const override { return "Cardputer internal keyboard"; }

    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;

    bool isAttached() const override { return true; }

    lv_indev_t* getLvglIndev() override { return lvglDevice; }
};
