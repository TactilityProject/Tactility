#include "Power.h"

#include <driver/adc.h>
#include <tactility/log.h>

constexpr auto* TAG = "Power";

bool Power::adcInitCalibration() {
    bool calibrated = false;

    esp_err_t efuse_read_result = esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP_FIT);
    if (efuse_read_result == ESP_ERR_NOT_SUPPORTED) {
        LOG_W(TAG, "Calibration scheme not supported, skip software calibration");
    } else if (efuse_read_result == ESP_ERR_INVALID_VERSION) {
        LOG_W(TAG, "eFuse not burnt, skip software calibration");
    } else if (efuse_read_result == ESP_OK) {
        calibrated = true;
        LOG_I(TAG, "Calibration success");
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, static_cast<adc_bits_width_t>(ADC_WIDTH_BIT_DEFAULT), 0, &adcCharacteristics);
    } else {
        LOG_W(TAG, "eFuse read failed, skipping calibration");
    }

    return calibrated;
}

uint32_t Power::adcReadValue() const {
    int adc_raw = adc1_get_raw(ADC1_CHANNEL_4);
    LOG_D(TAG, "Raw data: %d", adc_raw);

    uint32_t voltage;

    if (calibrated) {
        voltage = esp_adc_cal_raw_to_voltage(adc_raw, &adcCharacteristics);
        LOG_D(TAG, "Calibrated data: %d mV", (int)voltage);
    } else {
        voltage = (adc_raw * 3300) / 4095; // fallback
        LOG_D(TAG, "Estimated data: %d mV", (int)voltage);
    }

    return voltage;
}   

bool Power::ensureInitialized() {
    if (!initialized) {

        if (adc1_config_width(ADC_WIDTH_BIT_12) != ESP_OK) {
            LOG_E(TAG, "ADC1 config width failed");
            return false;
        }

        if (adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11) != ESP_OK) {
            LOG_E(TAG, "ADC1 config attenuation failed");
            return false;
        }

        calibrated = adcInitCalibration();

        initialized = true;
    }
    return true;
}

bool Power::supportsMetric(MetricType type) const {
    switch (type) {
        using enum MetricType;
        case BatteryVoltage:
        case ChargeLevel:
            return true;
        default:
            return false;
    }
}

bool Power::getMetric(MetricType type, MetricData& data) {
    if (!ensureInitialized()) {
        return false;
    }

    switch (type) {
        case MetricType::BatteryVoltage:
            data.valueAsUint32 = adcReadValue() * 2;
            return true;
        case MetricType::ChargeLevel:
            data.valueAsUint8 = chargeFromAdcVoltage.estimateCharge(adcReadValue() * 2);
            return true;
        default:
            return false;
    }
}
