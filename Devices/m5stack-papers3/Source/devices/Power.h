#pragma once

#include <memory>
#include <ChargeFromAdcVoltage.h>
#include <Tactility/hal/power/PowerDevice.h>
#include <driver/gpio.h>

using tt::hal::power::PowerDevice;

/**
 * @brief Power management for M5Stack PaperS3
 *
 * Hardware configuration:
 * - Battery voltage: GPIO3 (ADC1_CHANNEL_2) with 2x voltage divider
 * - Charge status: GPIO4 - digital signal (0 = charging, 1 = not charging)
 * - USB detect: GPIO5 - digital signal (1 = USB connected)
 * - Power off: GPIO44 - pull high to trigger shutdown
 */
class PaperS3Power final : public PowerDevice {
private:
    std::unique_ptr<::ChargeFromAdcVoltage> chargeFromAdcVoltage;
    gpio_num_t powerOffPin;
    bool powerOffInitialized = false;
    bool buzzerInitialized = false;

public:
    explicit PaperS3Power(
        std::unique_ptr<::ChargeFromAdcVoltage> chargeFromAdcVoltage,
        gpio_num_t powerOffPin
    );
    ~PaperS3Power() override = default;

    std::string getName() const override { return "M5Stack PaperS3 Power"; }
    std::string getDescription() const override { return "Battery monitoring with charge detection and power-off"; }

    bool supportsMetric(MetricType type) const override;
    bool getMetric(MetricType type, MetricData& data) override;

    bool supportsPowerOff() const override { return true; }
    void powerOff() override;

private:
    void initializePowerOff();
    bool isCharging();
    // TODO: Fix USB Detection
    bool isUsbConnected();

    // Buzzer functions only used for the power off signal sound.
    // So the user actually knows the epaper display is turning off.
    void buzzerLedcInit();
    void toneOn(int frequency, int duration);
    void toneOff();
};

std::shared_ptr<tt::hal::power::PowerDevice> createPower();
