#include "BatteryManager.h"

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/log.h>

#include <drivers/bq24295.h>

constexpr auto* TAG = "unPhoneFeatures";

namespace expanderpin {
    static constexpr gpio_pin_t EXPANDER_POWER = 0; // enable exp brd if high
}

bool BatteryManager::initGpioExpander() {
    if (device_get_by_name("tca9535", &expander) != ERROR_NONE) {
        LOG_E(TAG, "IO expander device not found");
        return false;
    }

    expanderPowerPin = gpio_descriptor_acquire(expander, expanderpin::EXPANDER_POWER, GPIO_FLAG_DIRECTION_OUTPUT, GPIO_OWNER_GPIO);
    check(expanderPowerPin != nullptr);
    device_put(expander);

    return true;
}

bool BatteryManager::init() {
    LOG_I(TAG, "init");

    if (!initGpioExpander()) {
        LOG_E(TAG, "GPIO expander init failed");
        return false;
    }

    return true;
}

bool BatteryManager::setExpanderPower(bool on) const {
    return gpio_descriptor_set_level(expanderPowerPin, on) == ERROR_NONE;
}

void BatteryManager::turnPeripheralsOff() const {
    setExpanderPower(false);

    Device* backlight = nullptr;
    check(device_get_by_name("display_backlight", &backlight) == ERROR_NONE);
    backlight_set_brightness(backlight, 0); // Allowed to fail, we don't care about the result
    device_put(backlight);
}

void BatteryManager::setShipping(bool on) const {
    if (on) {
        LOG_W(TAG, "setShipping: on");
        bq24295_set_watchdog_timer(batteryManagement, BQ24295_WATCHDOG_DISABLED);
        bq24295_set_bat_fet_on(batteryManagement, false);
    } else {
        LOG_W(TAG, "setShipping: off");
        bq24295_set_watchdog_timer(batteryManagement, BQ24295_WATCHDOG_ENABLED_40S);
        bq24295_set_bat_fet_on(batteryManagement, true);
    }
}

bool BatteryManager::isUsbPowerConnected() const {
    bool connected = false;
    return bq24295_is_usb_power_connected(batteryManagement, &connected) == ERROR_NONE && connected;
}
