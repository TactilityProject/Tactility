#pragma once

#include <string>
#include <esp_adc_cal.h>
#include <driver/gpio.h>
#include <ChargeFromVoltage.h>
#include <Tactility/hal/power/PowerDevice.h>

constexpr auto THMI_POWEREN_GPIO = GPIO_NUM_10;
constexpr auto THMI_POWERON_GPIO = GPIO_NUM_14;

using tt::hal::power::PowerDevice;

class Power final : public PowerDevice {

    ChargeFromVoltage chargeFromAdcVoltage = ChargeFromVoltage(3.3f, 4.2f);
    esp_adc_cal_characteristics_t adcCharacteristics;
    bool initialized = false;
    bool calibrated = false;

    bool adcInitCalibration();
    uint32_t adcReadValue() const;

    bool ensureInitialized();

public:

    std::string getName() const override { return "T-HMI Power"; }
    std::string getDescription() const override { return "Power measurement via ADC"; }

    bool supportsMetric(MetricType type) const override;
    bool getMetric(MetricType type, MetricData& data) override;
};
