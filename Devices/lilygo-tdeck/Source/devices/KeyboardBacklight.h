#pragma once

#include <tactility/hal/Device.h>
#include <Tactility/TactilityCore.h>

class KeyboardBacklightDevice final : public tt::hal::Device {

    bool initialized = false;

public:

    tt::hal::Device::Type getType() const override { return tt::hal::Device::Type::I2c; }
    std::string getName() const override { return "Keyboard Backlight"; }
    std::string getDescription() const override { return "T-Deck keyboard backlight control"; }

    bool start();
    bool stop();
    bool isAttached() const { return initialized; }
    
    /**
     * Set keyboard backlight brightness
     * @param brightness 0-255 (0=off, 255=max)
     */
    bool setBrightness(uint8_t brightness);
    
    /**
     * Get current brightness
     * @return 0-255
     */
    uint8_t getBrightness() const;
};

