#include "KeyboardBacklight.h"
#include <KeyboardBacklight/KeyboardBacklight.h>  // Driver
#include <Tactility/hal/i2c/I2c.h>
#include <Tactility/settings/KeyboardSettings.h>

// TODO: Add Mutex and consider refactoring into a class
bool KeyboardBacklightDevice::start() {
    if (initialized) {
        return true;
    }

    // T-Deck uses I2C_NUM_0 for internal peripherals
    initialized = keyboardbacklight::init(I2C_NUM_0);
    if (!initialized) {
        return false;
    }

    // Backlight doesn't seem to turn on until toggled on and off from keyboard settings...
    // Or let the display and backlight sleep then wake it up.
    // Then it works fine...until reboot, then you need to toggle again.
    // The current keyboard firmware sets backlight duty to 0 on boot.
    // https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/firmware/T-Keyboard_Keyboard_ESP32C3_250620.bin
    // https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino#L25
    // https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino#L217
    auto kbSettings = tt::settings::keyboard::loadOrGetDefault();
    keyboardbacklight::setBrightness(kbSettings.backlightEnabled ? kbSettings.backlightBrightness : 0);

    return true;
}

bool KeyboardBacklightDevice::stop() {
    if (initialized) {
        // Turn off backlight on shutdown
        keyboardbacklight::setBrightness(0);
        initialized = false;
    }
    return true;
}

bool KeyboardBacklightDevice::setBrightness(uint8_t brightness) {
    if (!initialized) {
        return false;
    }
    return keyboardbacklight::setBrightness(brightness);
}

uint8_t KeyboardBacklightDevice::getBrightness() const {
    if (!initialized) {
        return 0;
    }
    return keyboardbacklight::getBrightness();
}
