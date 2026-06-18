#pragma once

#include <Tactility/hal/keyboard/KeyboardDevice.h>
#include <Tactility/TactilityCore.h>

struct Device;

class TdeckKeyboard final : public tt::hal::keyboard::KeyboardDevice {

    Device* i2cController;
    lv_indev_t* deviceHandle = nullptr;

public:

    explicit TdeckKeyboard(Device* i2cController) : i2cController(i2cController) {}

    std::string getName() const override { return "T-Deck Keyboard"; }
    std::string getDescription() const override { return "I2C keyboard"; }

    Device* getI2cController() const { return i2cController; }

    bool startLvgl(lv_display_t* display) override;
    bool stopLvgl() override;
    bool isAttached() const override;
    lv_indev_t* getLvglIndev() override { return deviceHandle; }
};
