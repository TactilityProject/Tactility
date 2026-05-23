#include "Power.h"

#include <Tactility/hal/power/PowerDevice.h>
#include <drivers/ina226.h>
#include <tactility/device.h>
#include <tactility/log.h>

using namespace tt::hal::power;

static constexpr auto* TAG = "StackChanPower";

// 1S Li-ion cell (550 mAh): 3.2V (empty) – 4.2V (full)
static constexpr float MIN_BATTERY_VOLTAGE_MV = 3200.0f;
static constexpr float MAX_BATTERY_VOLTAGE_MV = 4200.0f;

class StackChanPower final : public PowerDevice {
public:
    explicit StackChanPower(::Device* ina226Device) : ina226(ina226Device) {}

    std::string getName() const override { return "M5Stack StackChan Power"; }
    std::string getDescription() const override { return "Battery monitoring via INA226 over I2C"; }

    bool supportsMetric(MetricType type) const override {
        switch (type) {
            using enum MetricType;
            case BatteryVoltage:
            case ChargeLevel:
            case Current:
                return ina226 != nullptr;
            default:
                return false;
        }
    }

    bool getMetric(MetricType type, MetricData& data) override {
        switch (type) {
            using enum MetricType;

            case BatteryVoltage: {
                if (ina226 == nullptr) return false;
                float volts = 0.0f;
                if (ina226_read_bus_voltage(ina226, &volts) != ERROR_NONE) return false;
                data.valueAsUint32 = static_cast<uint32_t>(volts * 1000.0f);
                return true;
            }

            case ChargeLevel: {
                if (ina226 == nullptr) return false;
                float volts = 0.0f;
                if (ina226_read_bus_voltage(ina226, &volts) != ERROR_NONE) return false;
                float voltage_mv = volts * 1000.0f;
                if (voltage_mv >= MAX_BATTERY_VOLTAGE_MV) {
                    data.valueAsUint8 = 100;
                } else if (voltage_mv <= MIN_BATTERY_VOLTAGE_MV) {
                    data.valueAsUint8 = 0;
                } else {
                    float factor = (voltage_mv - MIN_BATTERY_VOLTAGE_MV) / (MAX_BATTERY_VOLTAGE_MV - MIN_BATTERY_VOLTAGE_MV);
                    data.valueAsUint8 = static_cast<uint8_t>(factor * 100.0f);
                }
                return true;
            }

            case Current: {
                if (ina226 == nullptr) return false;
                float amps = 0.0f;
                if (ina226_read_shunt_current(ina226, &amps) != ERROR_NONE) return false;
                data.valueAsInt32 = static_cast<int32_t>(amps * 1000.0f);
                return true;
            }

            default:
                return false;
        }
    }

private:
    ::Device* ina226;
};

std::shared_ptr<PowerDevice> createPower() {
    auto* ina226 = device_find_by_name("ina226");
    if (ina226 == nullptr) {
        LOG_E(TAG, "ina226 device not found");
    }
    return std::make_shared<StackChanPower>(ina226);
}
