#pragma once

#include <memory>
#include <ChargeFromAdcVoltage.h>
#include <Tactility/hal/power/PowerDevice.h>
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>

namespace tt::hal::power {

/**
 * @brief Power management for M5Stack PaperS3
 * 
 * Hardware configuration:
 * - Battery voltage: GPIO3 (ADC1_CHANNEL_2) with 2x voltage divider
 * - Charge status: GPIO4 (ADC1_CHANNEL_3) - analog signal
 *   * ~0.01V when not charging
 *   * ~0.25-0.3V when charging
 * - Power off: GPIO44 - pull high to trigger shutdown
 */
class PaperS3Power final : public PowerDevice {
private:
    std::unique_ptr<::ChargeFromAdcVoltage> chargeFromAdcVoltage;
    adc_oneshot_unit_handle_t adcHandle;
    adc_channel_t chargeStatusAdcChannel;
    gpio_num_t powerOffPin;
    bool chargeStatusInitialized = false;
    bool powerOffInitialized = false;

public:
    explicit PaperS3Power(
        std::unique_ptr<::ChargeFromAdcVoltage> chargeFromAdcVoltage,
        adc_oneshot_unit_handle_t adcHandle,
        adc_channel_t chargeStatusAdcChannel,
        gpio_num_t powerOffPin
    );
    ~PaperS3Power() override;

    std::string getName() const override { return "M5Stack PaperS3 Power"; }
    std::string getDescription() const override { return "Battery monitoring with charge detection and power-off"; }

    // Metric support
    bool supportsMetric(MetricType type) const override;
    bool getMetric(MetricType type, MetricData& data) override;

    // Power off support
    bool supportsPowerOff() const override { return true; }
    void powerOff() override;

private:
    void initializeChargeStatus();
    void initializePowerOff();
    bool isCharging();
};

} // namespace tt::hal::power

std::shared_ptr<tt::hal::power::PowerDevice> createPower();
