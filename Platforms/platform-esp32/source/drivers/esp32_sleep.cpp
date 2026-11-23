// SPDX-License-Identifier: Apache-2.0
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/soc_caps.h>

#if SOC_RTCIO_PIN_COUNT > 0
#include <driver/rtc_io.h>
#endif

#include <tactility/driver.h>
#include <tactility/drivers/esp32_sleep.h>
#include <tactility/error_esp32.h>
#include <tactility/log.h>

#define TAG "esp32_sleep"
#define GET_CONFIG(device) (static_cast<const Esp32SleepConfig*>((device)->config))

static constexpr TickType_t RELEASE_TIMEOUT = pdMS_TO_TICKS(2000);
static constexpr TickType_t RELEASE_POLL_INTERVAL = pdMS_TO_TICKS(10);

extern "C" {

static bool is_active_low(const GpioPinSpec& spec) {
    return (spec.flags & GPIO_FLAG_ACTIVE_LOW) != 0;
}

static bool any_pin_active(const Esp32SleepConfig* config) {
    for (uint8_t i = 0; i < config->wakeup_gpios_count; i++) {
        const auto& spec = config->wakeup_gpios[i];
        bool physical_high = gpio_get_level(static_cast<gpio_num_t>(spec.pin)) != 0;
        if (physical_high != is_active_low(spec)) {
            return true;
        }
    }
    return false;
}

static error_t validate_config(const Esp32SleepConfig* config) {
    if (config->wakeup_gpios_count == 0) {
        return ERROR_INVALID_ARGUMENT;
    }
    bool active_low = is_active_low(config->wakeup_gpios[0]);
    for (uint8_t i = 0; i < config->wakeup_gpios_count; i++) {
        const auto& spec = config->wakeup_gpios[i];
        if (!esp_sleep_is_valid_wakeup_gpio(static_cast<gpio_num_t>(spec.pin))) {
            LOG_E(TAG, "GPIO %d can't wake from deep sleep", (int)spec.pin);
            return ERROR_NOT_SUPPORTED;
        }
        if (is_active_low(spec) != active_low) {
            LOG_E(TAG, "Wakeup pins must share the same active level");
            return ERROR_NOT_SUPPORTED;
        }
    }
#if CONFIG_IDF_TARGET_ESP32
    // ESP32 EXT1 can only wake on all pins low, so active-low wakeup uses EXT0 with a single pin
    if (active_low && config->wakeup_gpios_count > 1) {
        LOG_E(TAG, "ESP32 supports only a single active-low wakeup pin");
        return ERROR_NOT_SUPPORTED;
    }
#endif
    return ERROR_NONE;
}

static error_t start_device(Device* device) {
    return validate_config(GET_CONFIG(device));
}

#if SOC_RTCIO_PIN_COUNT > 0
// Targets with actual RTC-GPIO pads (ESP32, S3, C6, P4, ...): pulls survive deep sleep only on
// these pads, and EXT0/EXT1 are the wakeup sources that read them
static error_t deep_sleep(Device* device) {
    const auto* config = GET_CONFIG(device);

    uint64_t mask = 0;
    for (uint8_t i = 0; i < config->wakeup_gpios_count; i++) {
        const auto& spec = config->wakeup_gpios[i];
        auto pin = static_cast<gpio_num_t>(spec.pin);
        gpio_input_enable(pin);
        if (spec.flags & GPIO_FLAG_PULL_UP) {
            gpio_pullup_en(pin);
        }
        if (spec.flags & GPIO_FLAG_PULL_DOWN) {
            gpio_pulldown_en(pin);
        }
        mask |= 1ULL << spec.pin;
    }

    // Sleeping while a wakeup pin is still active wakes the system right away
    TickType_t waited = 0;
    while (any_pin_active(config) && waited < RELEASE_TIMEOUT) {
        vTaskDelay(RELEASE_POLL_INTERVAL);
        waited += RELEASE_POLL_INTERVAL;
    }

    // Digital pull resistors are powered down during deep sleep, so the RTC pulls hold the pin state
    bool needs_rtc_pulls = false;
    for (uint8_t i = 0; i < config->wakeup_gpios_count; i++) {
        const auto& spec = config->wakeup_gpios[i];
        auto pin = static_cast<gpio_num_t>(spec.pin);
        if (spec.flags & GPIO_FLAG_PULL_UP) {
            rtc_gpio_pullup_en(pin);
            needs_rtc_pulls = true;
        }
        if (spec.flags & GPIO_FLAG_PULL_DOWN) {
            rtc_gpio_pulldown_en(pin);
            needs_rtc_pulls = true;
        }
    }
    if (needs_rtc_pulls) {
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    }

    bool active_low = is_active_low(config->wakeup_gpios[0]);
    esp_err_t esp_error;
#if CONFIG_IDF_TARGET_ESP32
    if (active_low) {
        esp_error = esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(config->wakeup_gpios[0].pin), 0);
    } else {
        esp_error = esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_HIGH);
    }
#else
    esp_error = esp_sleep_enable_ext1_wakeup_io(mask, active_low ? ESP_EXT1_WAKEUP_ANY_LOW : ESP_EXT1_WAKEUP_ANY_HIGH);
#endif
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "Failed to configure wakeup: %s", esp_err_to_name(esp_error));
        return esp_err_to_error(esp_error);
    }

    LOG_I(TAG, "Entering deep sleep");
    esp_deep_sleep_start();
    return ERROR_NONE;
}
#else

static error_t deep_sleep(Device* device) {
    const auto* config = GET_CONFIG(device);

    uint64_t mask = 0;
    for (uint8_t i = 0; i < config->wakeup_gpios_count; i++) {
        const auto& spec = config->wakeup_gpios[i];
        auto pin = static_cast<gpio_num_t>(spec.pin);
        gpio_input_enable(pin);
        if (spec.flags & GPIO_FLAG_PULL_UP) {
            gpio_pullup_en(pin);
        }
        if (spec.flags & GPIO_FLAG_PULL_DOWN) {
            gpio_pulldown_en(pin);
        }
        mask |= 1ULL << spec.pin;
    }

    // Sleeping while a wakeup pin is still active wakes the system right away
    TickType_t waited = 0;
    while (any_pin_active(config) && waited < RELEASE_TIMEOUT) {
        vTaskDelay(RELEASE_POLL_INTERVAL);
        waited += RELEASE_POLL_INTERVAL;
    }

    bool active_low = is_active_low(config->wakeup_gpios[0]);
    esp_err_t esp_error = esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(
        mask,
        active_low ? ESP_GPIO_WAKEUP_GPIO_LOW : ESP_GPIO_WAKEUP_GPIO_HIGH
    );
    if (esp_error != ESP_OK) {
        LOG_E(TAG, "Failed to configure wakeup: %s", esp_err_to_name(esp_error));
        return esp_err_to_error(esp_error);
    }

    LOG_I(TAG, "Entering deep sleep");
    esp_deep_sleep_start();
    return ERROR_NONE;
}
#endif

static const SleepControllerApi esp32_sleep_api = {
    .deep_sleep = deep_sleep
};

Driver esp32_sleep_driver = {
    .name = "esp32_sleep",
    .compatible = (const char*[]) { "espressif,esp32-sleep", nullptr },
    .start_device = start_device,
    .stop_device = nullptr,
    .probe = nullptr,
    .api = &esp32_sleep_api,
    .device_type = &SLEEP_CONTROLLER_TYPE,
    .internal = nullptr
};

} // extern "C"
