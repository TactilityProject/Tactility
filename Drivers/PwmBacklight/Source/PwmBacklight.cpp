#include "PwmBacklight.h"

#include <tactility/log.h>

constexpr auto* TAG = "PwmBacklight";

namespace driver::pwmbacklight {

static bool isBacklightInitialized = false;
static gpio_num_t backlightPin = GPIO_NUM_NC;
static ledc_timer_t backlightTimer;
static ledc_channel_t backlightChannel;

bool init(gpio_num_t pin, uint32_t frequencyHz, ledc_timer_t timer, ledc_channel_t channel) {
    backlightPin = pin;
    backlightTimer = timer;
    backlightChannel = channel;

    LOG_I(TAG, "Init");
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = backlightTimer,
        .freq_hz = frequencyHz,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false
    };

    if (ledc_timer_config(&ledc_timer) != ESP_OK) {
        LOG_E(TAG, "Timer config failed");
        return false;
    }

    ledc_channel_config_t ledc_channel = {
        .gpio_num = backlightPin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = backlightChannel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = backlightTimer,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = 0
        }
    };

    if (ledc_channel_config(&ledc_channel) != ESP_OK) {
        LOG_E(TAG, "Channel config failed");
        return false;
    }

    isBacklightInitialized = true;

    return true;
}

bool setBacklightDuty(uint8_t duty) {
    if (!isBacklightInitialized) {
        LOG_E(TAG, "Not initialized");
        return false;
    }
    return ledc_set_duty(LEDC_LOW_SPEED_MODE, backlightChannel, duty) == ESP_OK &&
    ledc_update_duty(LEDC_LOW_SPEED_MODE, backlightChannel) == ESP_OK;
}

}
