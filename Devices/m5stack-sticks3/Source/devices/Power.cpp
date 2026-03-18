#include "Power.h"

#include <Tactility/hal/i2c/I2c.h>
#include <Tactility/hal/power/PowerDevice.h>
#include <tactility/log.h>
#include <driver/i2c.h>

using namespace tt::hal::power;

static constexpr auto* TAG = "StickS3Power";

static constexpr i2c_port_t M5PM1_I2C_PORT  = I2C_NUM_0;
static constexpr uint8_t    M5PM1_ADDR       = 0x6E;

// Battery voltage: little-endian 16-bit in mV
static constexpr uint8_t REG_BAT_L  = 0x22;
// GPIO input register: bit 0 = PM1_G0 (charging status, LOW = charging)
static constexpr uint8_t REG_GPIO_IN = 0x12;
// Power-off: write 0xA1 (high nibble 0xA, low nibble 0x1) to trigger shutdown
static constexpr uint8_t REG_POWEROFF = 0x0C;

static constexpr float MIN_BATTERY_VOLTAGE_MV = 3300.0f;
static constexpr float MAX_BATTERY_VOLTAGE_MV = 4200.0f;

class StickS3Power final : public PowerDevice {
public:
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
                if (!readBatteryVoltage(mv)) return false;
                data.valueAsUint32 = mv;
                return true;
            }

            case ChargeLevel: {
                uint16_t mv = 0;
                if (!readBatteryVoltage(mv)) return false;
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
                uint8_t gpio_in = 0;
                if (!tt::hal::i2c::masterReadRegister(M5PM1_I2C_PORT, M5PM1_ADDR, REG_GPIO_IN, &gpio_in, 1)) {
                    LOG_W(TAG, "Failed to read GPIO input register");
                    return false;
                }
                // PM1_G0: LOW = charging, HIGH = not charging
                data.valueAsBool = (gpio_in & 0x01U) == 0;
                return true;
            }

            default:
                return false;
        }
    }

    bool supportsPowerOff() const override { return true; }

    void powerOff() override {
        LOG_W(TAG, "Powering off via M5PM1");
        // High nibble must be 0xA; low nibble 1 = shutdown
        uint8_t value = 0xA1;
        if (!tt::hal::i2c::masterWriteRegister(M5PM1_I2C_PORT, M5PM1_ADDR, REG_POWEROFF, &value, 1)) {
            LOG_E(TAG, "Failed to send power-off command");
        }
    }

private:
    bool readBatteryVoltage(uint16_t& mv) {
        uint8_t buf[2] = {};
        if (!tt::hal::i2c::masterReadRegister(M5PM1_I2C_PORT, M5PM1_ADDR, REG_BAT_L, buf, sizeof(buf))) {
            LOG_W(TAG, "Failed to read battery voltage");
            return false;
        }
        mv = static_cast<uint16_t>(buf[0] | (buf[1] << 8));
        return true;
    }
};

std::shared_ptr<PowerDevice> createPower() {
    return std::make_shared<StickS3Power>();
}
