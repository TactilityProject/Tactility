#pragma once

#include <tactility/hal/Device.h>
#include <cstdint>
#include <string>

namespace tt::hal::power {

class PowerDevice : public Device {

public:

    PowerDevice();
    ~PowerDevice() override;

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

    virtual bool supportsQuickCharge() const { return false; }
    virtual bool isQuickChargeEnabled() const { return false; }
    virtual void setQuickChargeEnabled(bool enabled) { /* NO-OP */ }

    virtual bool supportsPowerOff() const { return false; }
    virtual void powerOff() { /* NO-OP*/ }

private:

    /** Creates the kernel-level power_supply device that exposes this instance to TactilityKernel. */
    void createPowerSupplyDevice();

    /** Destroys the kernel-level power_supply device created by createPowerSupplyDevice(). */
    void destroyPowerSupplyDevice();

    std::string kernelDeviceName;
    KernelDevice kernelDevice {};
};

}
