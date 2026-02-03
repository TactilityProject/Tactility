#pragma once

#include <ChargeFromVoltage.h>
#include <esp_adc/adc_oneshot.h>

class ChargeFromAdcVoltage {

public:

    struct Configuration {
        float adcMultiplier = 1.0f;
        float adcRefVoltage = 3.3f;
        adc_channel_t adcChannel = ADC_CHANNEL_3;
        adc_oneshot_unit_init_cfg_t adcConfig = {
            .unit_id = ADC_UNIT_1,
            .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        adc_oneshot_chan_cfg_t adcChannelConfig = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
    };

private:

    adc_oneshot_unit_handle_t adcHandle = nullptr;
    Configuration configuration;
    ChargeFromVoltage chargeFromVoltage;

public:

    explicit ChargeFromAdcVoltage(const Configuration& configuration, float voltageMin = 3.2f, float voltageMax = 4.2f);

    ~ChargeFromAdcVoltage();

    bool readBatteryVoltageSampled(uint32_t& output) const;

    bool readBatteryVoltageOnce(uint32_t& output) const;

    uint8_t estimateChargeLevelFromVoltage(uint32_t milliVolt) const { return chargeFromVoltage.estimateCharge(milliVolt); }

    /**
     * @brief Get the ADC unit handle
     * 
     * Exposes the ADC unit handle to allow configuring additional channels
     * on the same ADC unit. Use with caution.
     * 
     * @return The ADC unit handle, or nullptr if not initialized
     */
    adc_oneshot_unit_handle_t getAdcHandle() const { return adcHandle; }
};
