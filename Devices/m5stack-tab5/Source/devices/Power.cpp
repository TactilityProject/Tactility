#include "Power.h"

#include <Tactility/hal/power/PowerDevice.h>
#include <drivers/ina226.h>
#include <tactility/device.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace tt::hal::power;

static constexpr auto* TAG = "Tab5Power";

// NP-F550 is a 2S Li-ion pack; INA226 measures the pack voltage on BAT_IN
// before the DC-DC converter. Per-cell range 3.2-4.2V → pack range 6.4-8.4V.
static constexpr float MIN_BATTERY_VOLTAGE_MV = 6400.0f;
static constexpr float MAX_BATTERY_VOLTAGE_MV = 8400.0f;

// INA226 convention: negative raw current = charging, positive = discharging.
// After negation in getMetric(Current), >50mA means charging into the battery.
static constexpr float CHARGING_CURRENT_THRESHOLD_AMPS = 0.05f;

// GPIO expander 1 (0x44) pin 4: PWROFF_PULSE
static constexpr int GPIO_EXP1_PIN_DEVICE_POWER = 4;
// GPIO expander 1 (0x44) pin 5: IP2326 nCHG_QC_EN (active-low: LOW = QC enabled)
static constexpr int GPIO_EXP1_PIN_IP2326_NCHG_QC_EN = 5;
// GPIO expander 1 (0x44) pin 7: IP2326 CHG_EN (HIGH = charging enabled, LOW = disabled)
static constexpr int GPIO_EXP1_PIN_IP2326_CHG_EN = 7;

class Tab5Power final : public PowerDevice {
public:
    Tab5Power(::Device* ina226Device, ::Device* ioExpander1Device)
        : ina226(ina226Device), ioExpander1(ioExpander1Device) {
        // Initialize CHG_EN as output HIGH (charging enabled at startup).
        setAllowedToCharge(true);
    }

    std::string getName() const override { return "M5Stack Tab5 Power"; }
    std::string getDescription() const override { return "Battery monitoring via INA226 over I2C"; }

    bool supportsMetric(MetricType type) const override {
        switch (type) {
            using enum MetricType;
            case BatteryVoltage:
            case ChargeLevel:
            case Current:
            case IsCharging:
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
                // INA226 convention: negative = charging, positive = discharging.
                // Negate so the HAL value is positive when charging, negative when discharging.
                data.valueAsInt32 = static_cast<int32_t>(-amps * 1000.0f);
                return true;
            }

            case IsCharging: {
                if (ina226 == nullptr) return false;
                float amps = 0.0f;
                if (ina226_read_shunt_current(ina226, &amps) != ERROR_NONE) return false;
                // Raw INA226: negative = charging. Threshold in raw terms = -0.05A.
                data.valueAsBool = amps < -CHARGING_CURRENT_THRESHOLD_AMPS;
                return true;
            }

            default:
                return false;
        }
    }

    bool supportsChargeControl() const override { return ioExpander1 != nullptr; }

    bool isAllowedToCharge() const override { return chargingAllowed; }

    void setAllowedToCharge(bool allowed) override {
        if (ioExpander1 == nullptr) return;
        auto* pin = gpio_descriptor_acquire(ioExpander1, GPIO_EXP1_PIN_IP2326_CHG_EN, GPIO_OWNER_GPIO);
        if (pin == nullptr) {
            LOG_W(TAG, "Failed to acquire CHG_EN pin");
            return;
        }
        if (gpio_descriptor_set_flags(pin, GPIO_FLAG_DIRECTION_OUTPUT) != ERROR_NONE) {
            LOG_W(TAG, "Failed to set CHG_EN pin direction");
            gpio_descriptor_release(pin);
            return;
        }
        if (gpio_descriptor_set_level(pin, allowed) != ERROR_NONE) {
            LOG_W(TAG, "Failed to set CHG_EN pin level");
            gpio_descriptor_release(pin);
            return;
        }
        gpio_descriptor_release(pin);
        chargingAllowed = allowed;
    }

    bool supportsQuickCharge() const override { return ioExpander1 != nullptr; }

    bool isQuickChargeEnabled() const override { return quickChargeEnabled; }

    void setQuickChargeEnabled(bool enabled) override {
        if (ioExpander1 == nullptr) return;
        auto* pin = gpio_descriptor_acquire(ioExpander1, GPIO_EXP1_PIN_IP2326_NCHG_QC_EN, GPIO_OWNER_GPIO);
        if (pin == nullptr) {
            LOG_W(TAG, "Failed to acquire nCHG_QC_EN pin");
            return;
        }
        if (gpio_descriptor_set_flags(pin, GPIO_FLAG_DIRECTION_OUTPUT) != ERROR_NONE) {
            LOG_W(TAG, "Failed to set nCHG_QC_EN pin direction");
            gpio_descriptor_release(pin);
            return;
        }
        if (gpio_descriptor_set_level(pin, !enabled) != ERROR_NONE) {
            LOG_W(TAG, "Failed to set nCHG_QC_EN pin level");
            gpio_descriptor_release(pin);
            return;
        }
        gpio_descriptor_release(pin);
        quickChargeEnabled = enabled;
    }

    bool supportsPowerOff() const override { return ioExpander1 != nullptr; }

    void powerOff() override {
        if (ioExpander1 == nullptr) return;
        auto* pin = gpio_descriptor_acquire(ioExpander1, GPIO_EXP1_PIN_DEVICE_POWER, GPIO_OWNER_GPIO);
        if (pin == nullptr) {
            LOG_E(TAG, "Failed to acquire DEVICE_POWER pin");
            return;
        }
        for (int i = 0; i < 3; i++) {
            gpio_descriptor_set_level(pin, true);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_descriptor_set_level(pin, false);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        gpio_descriptor_release(pin);
    }

private:
    ::Device* ina226;
    ::Device* ioExpander1;
    bool chargingAllowed = true;
    bool quickChargeEnabled = false;
};

std::shared_ptr<PowerDevice> createPower() {
    auto* ina226 = device_find_by_name("ina226");
    if (ina226 == nullptr) {
        LOG_E(TAG, "ina226 device not found");
    }
    auto* io_expander1 = device_find_by_name("io_expander1");
    if (io_expander1 == nullptr) {
        LOG_E(TAG, "io_expander1 not found");
    }
    return std::make_shared<Tab5Power>(ina226, io_expander1);
}
