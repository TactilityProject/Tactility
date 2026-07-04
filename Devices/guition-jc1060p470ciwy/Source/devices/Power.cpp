#include "Power.h"

#include <ChargeFromVoltage.h>
#include <tactility/log.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

using tt::hal::power::PowerDevice;

constexpr auto* TAG = "JcPower";

namespace {

constexpr adc_unit_t ADC_UNIT = ADC_UNIT_2;
constexpr adc_channel_t ADC_CHANNEL = ADC_CHANNEL_4; // matches ADC2 CH4 used in brookesia config
constexpr adc_atten_t ADC_ATTEN = ADC_ATTEN_DB_12;
constexpr int32_t UPPER_RESISTOR_OHM = 85'000;  // per brookesia settings
constexpr int32_t LOWER_RESISTOR_OHM = 100'000; // per brookesia settings

class JcPower final : public PowerDevice {
public:
    JcPower() : chargeEstimator(3.3f, 4.2f) {}
    ~JcPower() override { deinit(); }

    std::string getName() const override { return "JC Power"; }
    std::string getDescription() const override { return "Battery voltage via ADC"; }

    bool supportsMetric(MetricType type) const override {
        switch (type) {
            using enum MetricType;
            case BatteryVoltage:
            case ChargeLevel:
                return true;
            default:
                return false;
        }
    }

    bool getMetric(MetricType type, MetricData& data) override {
        if (!ensureInit()) {
            return false;
        }

        uint32_t batteryMv = 0;
        if (!readBatteryMilliVolt(batteryMv)) {
            return false;
        }

        switch (type) {
            case MetricType::BatteryVoltage:
                data.valueAsUint32 = batteryMv;
                return true;
            case MetricType::ChargeLevel:
                data.valueAsUint8 = chargeEstimator.estimateCharge(batteryMv);
                return true;
            default:
                return false;
        }
    }

private:
    bool ensureInit() {
        if (initialized) {
            return true;
        }

        adc_oneshot_unit_init_cfg_t init_cfg = {
            .unit_id = ADC_UNIT,
            .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        if (adc_oneshot_new_unit(&init_cfg, &adcHandle) != ESP_OK) {
            LOG_E(TAG, "ADC unit init failed");
            return false;
        }

        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten = ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_oneshot_config_channel(adcHandle, ADC_CHANNEL, &chan_cfg) != ESP_OK) {
            LOG_E(TAG, "ADC channel config failed");
            adc_oneshot_del_unit(adcHandle);
            adcHandle = nullptr;
            return false;
        }

        calibrated = tryInitCalibration();
        initialized = true;
        return true;
    }

    bool tryInitCalibration() {
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT,
            .atten = ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_line_fitting(&cali_config, &caliHandle) == ESP_OK) {
            calScheme = CaliScheme::Line;
            LOG_I(TAG, "ADC calibration (line fitting) enabled");
            return true;
        }
#endif

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t curve_cfg = {
            .unit_id = ADC_UNIT,
            .chan = ADC_CHANNEL,
            .atten = ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_curve_fitting(&curve_cfg, &caliHandle) == ESP_OK) {
            calScheme = CaliScheme::Curve;
            LOG_I(TAG, "ADC calibration (curve fitting) enabled");
            return true;
        }
#endif

        LOG_W(TAG, "ADC calibration not available, using raw scaling");
        return false;
    }

    bool readBatteryMilliVolt(uint32_t& outMv) {
        int raw = 0;
        if (adc_oneshot_read(adcHandle, ADC_CHANNEL, &raw) != ESP_OK) {
            LOG_E(TAG, "ADC read failed");
            return false;
        }

        int mv = 0;
        if (calibrated && adc_cali_raw_to_voltage(caliHandle, raw, &mv) == ESP_OK) {
            // ok
        } else {
            // Fallback: approximate assuming 12-bit full scale 3.3V
            mv = (raw * 3300) / 4095;
        }

        const int64_t numerator = static_cast<int64_t>(UPPER_RESISTOR_OHM + LOWER_RESISTOR_OHM) * mv;
        const int64_t denominator = LOWER_RESISTOR_OHM;
        outMv = static_cast<uint32_t>(numerator / denominator);
        return true;
    }

    void deinit() {
        if (adcHandle) {
            adc_oneshot_del_unit(adcHandle);
            adcHandle = nullptr;
        }
        if (caliHandle) {
            if (calScheme == CaliScheme::Line) {
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
                adc_cali_delete_scheme_line_fitting(caliHandle);
#endif
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
            } else if (calScheme == CaliScheme::Curve) {
                adc_cali_delete_scheme_curve_fitting(caliHandle);
#endif
            }
            caliHandle = nullptr;
            calibrated = false;
        }
    }

    enum class CaliScheme { None, Line, Curve };

    bool initialized = false;
    bool calibrated = false;
    CaliScheme calScheme = CaliScheme::None;
    adc_oneshot_unit_handle_t adcHandle = nullptr;
    adc_cali_handle_t caliHandle = nullptr;
    ChargeFromVoltage chargeEstimator;
};
} // namespace

std::shared_ptr<PowerDevice> createPower() {
    return std::make_shared<JcPower>();
}
