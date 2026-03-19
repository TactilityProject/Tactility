#include "Power.h"

#include <Tactility/hal/power/PowerDevice.h>
#include <drivers/m5pm1.h>
#include <tactility/device.h>
#include <tactility/log.h>

using namespace tt::hal::power;

static constexpr auto* TAG = "StickS3Power";

static constexpr float MIN_BATTERY_VOLTAGE_MV = 3300.0f;
static constexpr float MAX_BATTERY_VOLTAGE_MV = 4200.0f;

class StickS3Power final : public PowerDevice {
public:
    explicit StickS3Power(::Device* m5pm1Device) : m5pm1(m5pm1Device) {}

    std::string getName() const override { return "M5Stack StickS3 Power"; }
    std::string getDescription() const override { return "Battery monitoring via M5PM1 over I2C"; }

    bool supportsMetric(MetricType type) const override {
        switch (type) {
            using enum MetricType;
            case BatteryVoltage:
            case ChargeLevel:
            case IsCharging:
                return true;
            default:
                return false;
        }
    }

    bool getMetric(MetricType type, MetricData& data) override {
        switch (type) {
            using enum MetricType;

            case BatteryVoltage: {
                uint16_t mv = 0;
                if (m5pm1_get_battery_voltage(m5pm1, &mv) != ERROR_NONE) return false;
                data.valueAsUint32 = mv;
                return true;
            }

            case ChargeLevel: {
                uint16_t mv = 0;
                if (m5pm1_get_battery_voltage(m5pm1, &mv) != ERROR_NONE) return false;
                float voltage = static_cast<float>(mv);
                if (voltage >= MAX_BATTERY_VOLTAGE_MV) {
                    data.valueAsUint8 = 100;
                } else if (voltage <= MIN_BATTERY_VOLTAGE_MV) {
                    data.valueAsUint8 = 0;
                } else {
                    float factor = (voltage - MIN_BATTERY_VOLTAGE_MV) / (MAX_BATTERY_VOLTAGE_MV - MIN_BATTERY_VOLTAGE_MV);
                    data.valueAsUint8 = static_cast<uint8_t>(factor * 100.0f);
                }
                return true;
            }

            case IsCharging: {
                bool charging = false;
                if (m5pm1_is_charging(m5pm1, &charging) != ERROR_NONE) {
                    LOG_W(TAG, "Failed to read charging status");
                    return false;
                }
                data.valueAsBool = charging;
                return true;
            }

            default:
                return false;
        }
    }

    bool supportsPowerOff() const override { return true; }

    void powerOff() override {
        LOG_W(TAG, "Powering off via M5PM1");
        if (m5pm1_shutdown(m5pm1) != ERROR_NONE) {
            LOG_E(TAG, "Failed to send power-off command");
        }
    }

private:
    ::Device* m5pm1;
};

std::shared_ptr<PowerDevice> createPower() {
    auto* m5pm1 = device_find_by_name("m5pm1");
    if (m5pm1 == nullptr) {
        LOG_E(TAG, "m5pm1 device not found");
    }
    return std::make_shared<StickS3Power>(m5pm1);
}
