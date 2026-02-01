#pragma once

#include <Tactility/hal/keyboard/KeyboardDevice.h>
#include <Tactility/TactilityCore.h>

class TdeckKeyboard final : public tt::hal::keyboard::KeyboardDevice {

    lv_indev_t* deviceHandle = nullptr;

public:

    std::string getName() const override { return "T-Deck Keyboard"; }
    std::string getDescription() const override { return "I2C keyboard"; }

    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;
    bool isAttached() const override;
    lv_indev_t* getLvglIndev() override { return deviceHandle; }
};
