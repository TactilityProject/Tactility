#include "Power.h"

#include <tactility/log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/ledc.h>

using namespace tt::hal::power;

constexpr auto* TAG = "PaperS3Power";

// M5Stack PaperS3 hardware pin definitions
constexpr gpio_num_t VBAT_PIN = GPIO_NUM_3;  // Battery voltage with 2x divider
constexpr adc_channel_t VBAT_ADC_CHANNEL = ADC_CHANNEL_2;  // GPIO3 = ADC1_CHANNEL_2

constexpr gpio_num_t CHARGE_STATUS_PIN = GPIO_NUM_4;  // Charge IC status: 0 = charging, 1 = full/no USB
constexpr gpio_num_t USB_DETECT_PIN = GPIO_NUM_5;         // USB detect: 1 = USB connected
constexpr gpio_num_t POWER_OFF_PIN = GPIO_NUM_44;      // Pull high to trigger shutdown
constexpr gpio_num_t BUZZER_PIN = GPIO_NUM_21;

// Battery voltage divider ratio (voltage is divided by 2)
constexpr float VOLTAGE_DIVIDER_MULTIPLIER = 2.0f;

// Battery voltage range for LiPo batteries
constexpr float MIN_BATTERY_VOLTAGE = 3.3f;
constexpr float MAX_BATTERY_VOLTAGE = 4.2f;

// Power-off signal timing
constexpr int POWER_OFF_PULSE_COUNT = 5;
constexpr int POWER_OFF_PULSE_DURATION_MS = 100;

constexpr uint32_t BUZZER_DUTY_50_PERCENT = 4096;  // 50% of 13-bit (8192)

PaperS3Power::PaperS3Power(
    std::unique_ptr<ChargeFromAdcVoltage> chargeFromAdcVoltage,
    gpio_num_t powerOffPin
)
    : chargeFromAdcVoltage(std::move(chargeFromAdcVoltage)),
      powerOffPin(powerOffPin) {
    LOG_I(TAG, "Initialized M5Stack PaperS3 power management");
}

void PaperS3Power::buzzerLedcInit() {
    if (buzzerInitialized) {
        LOG_I(TAG, "Buzzer already initialized");
        return;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        LOG_E(TAG, "LEDC timer config failed: %s", esp_err_to_name(err));
        return;
    }

    ledc_channel_config_t channel_cfg = {
        .gpio_num = BUZZER_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = 0
        }
    };
    err = ledc_channel_config(&channel_cfg);
    if (err != ESP_OK) {
        LOG_E(TAG, "LEDC channel config failed: %s", esp_err_to_name(err));
        return;
    }

    buzzerInitialized = true;
}

void PaperS3Power::initializePowerOff() {
    if (powerOffInitialized) {
        return;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << powerOffPin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to configure power-off pin GPIO%d: %s", powerOffPin, esp_err_to_name(err));
        return;
    }

    gpio_set_level(powerOffPin, 0);
    powerOffInitialized = true;
    LOG_I(TAG, "Power-off control initialized on GPIO%d", powerOffPin);

    buzzerLedcInit();
}

// TODO: Fix USB Detection
bool PaperS3Power::isUsbConnected() {
    // USB_DETECT_PIN is configured as input with pull-down by initBoot() in Init.cpp.
    // Level 1 = USB VBUS present (per M5PaperS3 hardware spec).
    bool usbConnected = gpio_get_level(USB_DETECT_PIN) == 1;
    LOG_D(TAG, "USB_STATUS(GPIO%d)=%d", USB_DETECT_PIN, (int)usbConnected);
    return usbConnected;
}

bool PaperS3Power::isCharging() {
    // CHARGE_STATUS_PIN is configured as GPIO_MODE_INPUT by initBoot() in Init.cpp.
    int chargePin = gpio_get_level(CHARGE_STATUS_PIN);
    LOG_D(TAG, "CHG_STATUS(GPIO%d)=%d", CHARGE_STATUS_PIN, chargePin);
    return chargePin == 0;
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
            // isUsbConnected() is tracked separately but not used as a gate here:
            // when USB is absent the charge IC's CHG pin is inactive (high), so
            // isCharging() already returns false correctly.
            data.valueAsBool = isCharging();
            return true;

        default:
            return false;
    }
}

void PaperS3Power::toneOn(int frequency, int duration) {
    if (!buzzerInitialized) {
        LOG_I(TAG, "Buzzer not initialized");
        return;
    }

    if (frequency <= 0) {
        LOG_I(TAG, "Invalid frequency: %d", frequency);
        return;
    }

    esp_err_t err = ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, frequency);
    if (err != ESP_OK) {
        LOG_E(TAG, "LEDC set freq failed: %s", esp_err_to_name(err));
        return;
    }

    err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, BUZZER_DUTY_50_PERCENT);
    if (err != ESP_OK) {
        LOG_E(TAG, "LEDC set duty failed: %s", esp_err_to_name(err));
        return;
    }

    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (err != ESP_OK) {
        LOG_E(TAG, "LEDC update duty failed: %s", esp_err_to_name(err));
        return;
    }

    if (duration > 0) {
        vTaskDelay(pdMS_TO_TICKS(duration));
        toneOff();
    }
}

void PaperS3Power::toneOff() {
    if (!buzzerInitialized) {
        LOG_I(TAG, "Buzzer not initialized");
        return;
    }

    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    if (err != ESP_OK) {
        LOG_E(TAG, "LEDC set duty failed: %s", esp_err_to_name(err));
        return;
    }

    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (err != ESP_OK) {
        LOG_E(TAG, "LEDC update duty failed: %s", esp_err_to_name(err));
        return;
    }
}

void PaperS3Power::powerOff() {
    LOG_W(TAG, "Power-off requested");
    // Note: callers are responsible for stopping the display (e.g. EPD refresh) before
    // calling powerOff(). The beep sequence below (~500 ms) provides some lead time,
    // but a full EPD refresh can take up to ~1500 ms. If a refresh is still in flight
    // when GPIO44 cuts power, the current frame will be incomplete; the display will
    // recover correctly on next boot via a full-screen clear.

    if (!powerOffInitialized) {
        initializePowerOff();
        if (!powerOffInitialized) {
            LOG_E(TAG, "Power-off failed: GPIO not initialized");
            return;
        }
    }

    //beep on
    toneOn(440, 200);
    vTaskDelay(pdMS_TO_TICKS(100));
    //beep on
    toneOn(440, 200);

    LOG_W(TAG, "Triggering shutdown via GPIO%d (sending %d pulses)...", powerOffPin, POWER_OFF_PULSE_COUNT);

    for (int i = 0; i < POWER_OFF_PULSE_COUNT; i++) {
        gpio_set_level(powerOffPin, 1);
        vTaskDelay(pdMS_TO_TICKS(POWER_OFF_PULSE_DURATION_MS));
        gpio_set_level(powerOffPin, 0);
        vTaskDelay(pdMS_TO_TICKS(POWER_OFF_PULSE_DURATION_MS));
    }

    gpio_set_level(powerOffPin, 1);  // Final high state

    LOG_W(TAG, "Shutdown signal sent. Waiting for power-off...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    LOG_E(TAG, "Device did not power off as expected");
}

std::shared_ptr<PowerDevice> createPower() {
    ChargeFromAdcVoltage::Configuration config = {
        .adcMultiplier = VOLTAGE_DIVIDER_MULTIPLIER,
        .adcRefVoltage = 3.3f,
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

    auto adc = std::make_unique<ChargeFromAdcVoltage>(config, MIN_BATTERY_VOLTAGE, MAX_BATTERY_VOLTAGE);
    if (!adc->isInitialized()) {
        LOG_E(TAG, "ADC initialization failed; power monitoring unavailable");
        return nullptr;
    }

    return std::make_shared<PaperS3Power>(std::move(adc), POWER_OFF_PIN);
}
