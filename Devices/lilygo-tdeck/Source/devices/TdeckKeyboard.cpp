#include "TdeckKeyboard.h"

#include <KeyboardBacklight/KeyboardBacklight.h>
#include <Tactility/Logger.h>
#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/settings/DisplaySettings.h>
#include <Tactility/settings/KeyboardSettings.h>
#include <lvgl.h>
#include <tactility/drivers/i2c_controller.h>

using tt::hal::findFirstDevice;

static const auto LOGGER = tt::Logger("TdeckKeyboard");

constexpr uint8_t TDECK_KEYBOARD_SLAVE_ADDRESS = 0x55;

/**
 * The callback simulates press and release events, because the T-Deck
 * keyboard only publishes press events on I2C.
 * LVGL currently works without those extra release events, but they
 * are implemented for correctness and future compatibility.
 *
 * @param indev_drv
 * @param data
 */
static void keyboard_read_callback(lv_indev_t* indev, lv_indev_data_t* data) {
    static uint8_t last_buffer = 0x00;
    uint8_t read_buffer = 0x00;

    // Defaults
    data->key = 0;
    data->state = LV_INDEV_STATE_RELEASED;

    auto* keyboard = static_cast<TdeckKeyboard*>(lv_indev_get_user_data(indev));
    if (i2c_controller_read(keyboard->getI2cController(), TDECK_KEYBOARD_SLAVE_ADDRESS, &read_buffer, 1, 100 / portTICK_PERIOD_MS) == ERROR_NONE) {
        if (read_buffer == 0 && read_buffer != last_buffer) {
            if (LOGGER.isLoggingDebug()) {
                LOGGER.debug("Released {}", last_buffer);
            }
            data->key = last_buffer;
            data->state = LV_INDEV_STATE_RELEASED;
        } else if (read_buffer != 0) {
            if (LOGGER.isLoggingDebug()) {
                LOGGER.debug("Pressed {}", read_buffer);
            }
            data->key = read_buffer;
            data->state = LV_INDEV_STATE_PRESSED;
            // TODO: Avoid performance hit by calling loadOrGetDefault() on each key press
            // Ensure LVGL activity is triggered so idle services can wake the display
            lv_display_trigger_activity(nullptr);

            // Actively wake display/backlights immediately on key press (independent of idle tick)
            // Restore display backlight if off (we assume duty 0 means dimmed)
            auto display = findFirstDevice<tt::hal::display::DisplayDevice>(tt::hal::Device::Type::Display);
            if (display && display->supportsBacklightDuty()) {
                // Load display settings for target duty
                auto dsettings = tt::settings::display::loadOrGetDefault();
                // Always set duty, harmless if already on
                display->setBacklightDuty(dsettings.backlightDuty);
            }

            // Restore keyboard backlight if enabled in settings
            auto ksettings = tt::settings::keyboard::loadOrGetDefault();
            if (ksettings.backlightEnabled) {
                keyboardbacklight::setBrightness(ksettings.backlightBrightness);
            }
        }
    }

    last_buffer = read_buffer;
}

bool TdeckKeyboard::startLvgl(lv_display_t* display) {
    deviceHandle = lv_indev_create();
    lv_indev_set_type(deviceHandle, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(deviceHandle, &keyboard_read_callback);
    lv_indev_set_display(deviceHandle, display);
    lv_indev_set_user_data(deviceHandle, this);
    return true;
}

bool TdeckKeyboard::stopLvgl() {
    lv_indev_delete(deviceHandle);
    deviceHandle = nullptr;
    return true;
}

bool TdeckKeyboard::isAttached() const {
    return i2c_controller_has_device_at_address(i2cController, TDECK_KEYBOARD_SLAVE_ADDRESS, 100) == ERROR_NONE;
}
