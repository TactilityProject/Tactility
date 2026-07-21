#pragma once

#include <tactility/check.h>
#include <tactility/device.h>

struct Device;
struct GpioDescriptor;

/**
 * Easy access to GPIO pins
 */
class BatteryManager final {

    ::Device* expander = nullptr;
    ::Device* batteryManagement = nullptr;
    ::GpioDescriptor* expanderPowerPin = nullptr;

    bool initGpioExpander();


public:

    explicit BatteryManager() {
        check(device_get_by_name("bq24295", &batteryManagement) == ERROR_NONE);
    }

    ~BatteryManager() {
        device_put(batteryManagement);
    }

    bool init();

    bool setExpanderPower(bool on) const;

    void turnPeripheralsOff() const;

    /** Battery management (BQ24295) will stop supplying power until USB connects */
    void setShipping(bool on) const;

    bool isUsbPowerConnected() const;
};
