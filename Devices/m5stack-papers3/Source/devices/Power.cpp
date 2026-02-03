#include "Power.hpp"

#include <Tactility/Log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

using namespace tt::hal::power;

constexpr auto* TAG = "PaperS3Power";

// M5Stack PaperS3 hardware pin definitions
constexpr gpio_num_t VBAT_PIN = GPIO_NUM_3;  // Battery voltage with 2x divider
constexpr adc_channel_t VBAT_ADC_CHANNEL = ADC_CHANNEL_2;  // GPIO3 = ADC1_CHANNEL_2

constexpr gpio_num_t CHARGE_STATUS_PIN = GPIO_NUM_4;  // Charge status analog signal
constexpr adc_channel_t CHARGE_STATUS_ADC_CHANNEL = ADC_CHANNEL_3;  // GPIO4 = ADC1_CHANNEL_3

constexpr gpio_num_t POWER_OFF_PIN = GPIO_NUM_44;  // Pull high to trigger shutdown

// Battery voltage divider ratio (voltage is divided by 2)
constexpr float VOLTAGE_DIVIDER_MULTIPLIER = 2.0f;

// Battery voltage range for LiPo batteries
constexpr float MIN_BATTERY_VOLTAGE = 3.3f;  // Minimum safe voltage
constexpr float MAX_BATTERY_VOLTAGE = 4.2f;  // Maximum charge voltage

// Charge status voltage threshold
// When not charging: ~0.01V (10mV)
// When charging: ~0.25-0.3V (250-300mV)
constexpr int CHARGING_VOLTAGE_THRESHOLD_MV = 100; 

// Power-off signal timing
constexpr int POWER_OFF_PULSE_COUNT = 5;  // Number of pulses to ensure shutdown
constexpr int POWER_OFF_PULSE_DURATION_MS = 100;  // Duration of each pulse

PaperS3Power::PaperS3Power(
    std::unique_ptr<ChargeFromAdcVoltage> chargeFromAdcVoltage,
    adc_oneshot_unit_handle_t adcHandle,
    adc_channel_t chargeStatusAdcChannel,
    gpio_num_t powerOffPin
)
    : chargeFromAdcVoltage(std::move(chargeFromAdcVoltage)),
      adcHandle(adcHandle),
      chargeStatusAdcChannel(chargeStatusAdcChannel),
      powerOffPin(powerOffPin) {
    
    TT_LOG_I(TAG, "Initialized M5Stack PaperS3 power management");
    TT_LOG_I(TAG, "  Battery voltage: GPIO%d (ADC1_CH%d)", VBAT_PIN, VBAT_ADC_CHANNEL);
    TT_LOG_I(TAG, "  Charge status: GPIO%d (ADC1_CH%d)", CHARGE_STATUS_PIN, chargeStatusAdcChannel);
    TT_LOG_I(TAG, "  Power off: GPIO%d", powerOffPin);
}

PaperS3Power::~PaperS3Power() {
    // GPIO will be cleaned up by system
    TT_LOG_I(TAG, "Power management shut down");
}

void PaperS3Power::initializeChargeStatus() {
    if (chargeStatusInitialized) {
        return;
    }

    // Configure the charge status ADC channel
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,  // 12dB attenuation for wider range
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    
    esp_err_t err = adc_oneshot_config_channel(adcHandle, chargeStatusAdcChannel, &config);
    if (err != ESP_OK) {
        TT_LOG_E(TAG, "Failed to configure charge status ADC channel: %s", esp_err_to_name(err));
        return;
    }

    chargeStatusInitialized = true;
    TT_LOG_I(TAG, "Charge status monitoring initialized");
}

void PaperS3Power::initializePowerOff() {
    if (powerOffInitialized) {
        return;
    }

    // Configure power-off pin as output, initially low
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << powerOffPin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        TT_LOG_E(TAG, "Failed to configure power-off pin GPIO%d: %s", powerOffPin, esp_err_to_name(err));
        return;
    }
    
    gpio_set_level(powerOffPin, 0);  // Start low
    
    powerOffInitialized = true;
    TT_LOG_I(TAG, "Power-off control initialized on GPIO%d", powerOffPin);
}

bool PaperS3Power::isCharging() {
    if (!chargeStatusInitialized) {
        initializeChargeStatus();
        if (!chargeStatusInitialized) {
            return false;
        }
    }

    int adc_raw = 0;
    esp_err_t ret = adc_oneshot_read(adcHandle, chargeStatusAdcChannel, &adc_raw);
    
    if (ret != ESP_OK) {
        TT_LOG_E(TAG, "Failed to read charge status ADC: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Convert raw ADC value to voltage (approximate)
    // For 12-bit ADC with 12dB attenuation: 0-4095 maps to roughly 0-3100mV
    int voltage_mv = 0;
    static adc_cali_handle_t s_cali = nullptr;
    if (!s_cali) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .chan = CHARGE_STATUS_ADC_CHANNEL,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali) != ESP_OK) {
            TT_LOG_W(TAG, "ADC calibration unavailable; falling back to rough conversion");
        }
    }
    if (s_cali && adc_cali_raw_to_voltage(s_cali, adc_raw, &voltage_mv) != ESP_OK) {
        TT_LOG_W(TAG, "Calibration failed; falling back to rough conversion");
        voltage_mv = (adc_raw * 3100) / 4095;
    } else if (!s_cali) {
        voltage_mv = (adc_raw * 3100) / 4095;
    }
    
    bool charging = voltage_mv > CHARGING_VOLTAGE_THRESHOLD_MV;
    
    TT_LOG_D(TAG, "Charge status: raw=%d, voltage=%dmV, charging=%s", 
             adc_raw, voltage_mv, charging ? "YES" : "NO");
    
    return charging;
}

bool PaperS3Power::supportsMetric(MetricType type) const {
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

bool PaperS3Power::getMetric(MetricType type, MetricData& data) {
    switch (type) {
        using enum MetricType;
        
        case BatteryVoltage:
            return chargeFromAdcVoltage->readBatteryVoltageSampled(data.valueAsUint32);
        
        case ChargeLevel: {
            uint32_t voltage = 0;
            if (chargeFromAdcVoltage->readBatteryVoltageSampled(voltage)) {
                data.valueAsUint8 = chargeFromAdcVoltage->estimateChargeLevelFromVoltage(voltage);
                return true;
            }
            return false;
        }
        
        case IsCharging:
            data.valueAsBool = isCharging();
            return true;
        
        default:
            return false;
    }
}

void PaperS3Power::powerOff() {
    TT_LOG_W(TAG, "Power-off requested");
    
    if (!powerOffInitialized) {
        initializePowerOff();
        if (!powerOffInitialized) {
            TT_LOG_E(TAG, "Power-off failed: GPIO not initialized");
            return;
        }
    }
    
    TT_LOG_W(TAG, "Triggering shutdown via GPIO%d (sending %d pulses)...", 
             powerOffPin, POWER_OFF_PULSE_COUNT);
    
    // Send multiple high pulses to ensure shutdown is triggered
    for (int i = 0; i < POWER_OFF_PULSE_COUNT; i++) {
        gpio_set_level(powerOffPin, 1);  // Pull high
        vTaskDelay(pdMS_TO_TICKS(POWER_OFF_PULSE_DURATION_MS));
        gpio_set_level(powerOffPin, 0);  // Pull low
        vTaskDelay(pdMS_TO_TICKS(POWER_OFF_PULSE_DURATION_MS));
    }
    
    // Final high state
    gpio_set_level(powerOffPin, 1);
    
    TT_LOG_W(TAG, "Shutdown signal sent. Waiting for power-off...");
    
    // If we're still running after a delay, log it
    vTaskDelay(pdMS_TO_TICKS(1000));
    TT_LOG_E(TAG, "Device did not power off as expected");
}

std::shared_ptr<PowerDevice> createPower() {
    // Configure ChargeFromAdcVoltage for battery voltage monitoring
    ChargeFromAdcVoltage::Configuration config = {
        .adcMultiplier = VOLTAGE_DIVIDER_MULTIPLIER,
        .adcRefVoltage = 3.3f,  // ESP32-S3 reference voltage
        .adcChannel = VBAT_ADC_CHANNEL,
        .adcConfig = {
            .unit_id = ADC_UNIT_1,
            .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        },
        .adcChannelConfig = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        },
    };

    // Create ChargeFromAdcVoltage with battery voltage range
    auto chargeFromAdcVoltage = std::make_unique<ChargeFromAdcVoltage>(
        config, 
        MIN_BATTERY_VOLTAGE, 
        MAX_BATTERY_VOLTAGE
    );

    // Get ADC handle to share with charge status monitoring
    adc_oneshot_unit_handle_t adcHandle = chargeFromAdcVoltage->getAdcHandle();
    
    if (adcHandle == nullptr) {
        TT_LOG_E(TAG, "Failed to get ADC handle");
        return nullptr;
    }
    
    // Create and return PaperS3Power instance
    return std::make_shared<PaperS3Power>(
        std::move(chargeFromAdcVoltage), 
        adcHandle,
        CHARGE_STATUS_ADC_CHANNEL,
        POWER_OFF_PIN
    );
}
