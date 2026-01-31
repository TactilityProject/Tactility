#pragma once

#include <tactility/hal/Device.h>
#include <cstdint>

namespace tt::hal::power {

class PowerDevice : public Device {

public:

    PowerDevice() = default;
    ~PowerDevice() override = default;

    Type getType() const override { return Type::Power; }

    enum class MetricType {
        IsCharging, // bool
        Current, // int32_t, mAh - battery current: either during charging (positive value) or discharging (negative value)
        BatteryVoltage, // uint32_t, mV
        ChargeLevel, // uint8_t [0, 100]
    };

    union MetricData {
        int32_t valueAsInt32 = 0;
        uint32_t valueAsUint32;
        uint8_t valueAsUint8;
        float valueAsFloat;
        bool valueAsBool;
    };

    virtual bool supportsMetric(MetricType type) const = 0;

    /**
     * @return false when metric is not supported or (temporarily) not available.
     */
    virtual bool getMetric(MetricType type, MetricData& data) = 0;

    virtual bool supportsChargeControl() const { return false; }
    virtual bool isAllowedToCharge() const { return false; }
    virtual void setAllowedToCharge(bool canCharge) { /* NO-OP*/ }

    virtual bool supportsPowerOff() const { return false; }
    virtual void powerOff() { /* NO-OP*/ }
};

}
