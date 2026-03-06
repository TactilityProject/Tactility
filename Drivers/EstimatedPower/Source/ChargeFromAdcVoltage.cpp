#include "ChargeFromAdcVoltage.h"
#include <Tactility/Logger.h>

static const auto LOGGER = tt::Logger("ChargeFromAdcV");
constexpr auto MAX_VOLTAGE_SAMPLES = 15;

ChargeFromAdcVoltage::ChargeFromAdcVoltage(
    const Configuration& configuration,
    float voltageMin,
    float voltageMax
) : configuration(configuration), chargeFromVoltage(voltageMin, voltageMax) {
    if (adc_oneshot_new_unit(&configuration.adcConfig, &adcHandle) != ESP_OK) {
        LOGGER.error("ADC config failed");
        return;
    }

    if (adc_oneshot_config_channel(adcHandle, configuration.adcChannel, &configuration.adcChannelConfig) != ESP_OK) {
        LOGGER.error("ADC channel config failed");

        adc_oneshot_del_unit(adcHandle);
        adcHandle = nullptr;
        return;
    }
}

ChargeFromAdcVoltage::~ChargeFromAdcVoltage() {
    if (adcHandle) {
        adc_oneshot_del_unit(adcHandle);
    }
}

bool ChargeFromAdcVoltage::readBatteryVoltageOnce(uint32_t& output) const {
    if (adcHandle == nullptr) {
        return false;
    }
    int raw;
    if (adc_oneshot_read(adcHandle, configuration.adcChannel, &raw) == ESP_OK) {
        output = configuration.adcMultiplier * ((1000.f * configuration.adcRefVoltage) / 4096.f) * (float)raw;
        if (LOGGER.isLoggingVerbose()) {
            LOGGER.verbose("Raw = {}, voltage = {}", raw, output);
        }
        return true;
    } else {
        LOGGER.error("Read failed");
        return false;
    }
}

bool ChargeFromAdcVoltage::readBatteryVoltageSampled(uint32_t& output) const {
    size_t samples_read = 0;
    uint32_t sample_accumulator = 0;
    uint32_t sample_read_buffer;

    for (size_t i = 0; i < MAX_VOLTAGE_SAMPLES; ++i) {
        if (readBatteryVoltageOnce(sample_read_buffer)) {
            sample_accumulator += sample_read_buffer;
            samples_read++;
        }
    }

    if (samples_read == 0) {
        return false;
    }

    output = sample_accumulator / samples_read;
    return true;
}
