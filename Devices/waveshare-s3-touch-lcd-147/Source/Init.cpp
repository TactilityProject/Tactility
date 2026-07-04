#include <Tactility/LogMessages.h>
#include <Tactility/TactilityCore.h>
#include <tactility/log.h>
#include <driver/ledc.h>

constexpr auto* TAG = "Waveshare";

constexpr auto LCD_BL_LEDC_TIMER = LEDC_TIMER_0;
constexpr auto LCD_BL_LEDC_MODE = LEDC_LOW_SPEED_MODE;
constexpr auto LCD_BL_LEDC_CHANNEL = LEDC_CHANNEL_0;
constexpr auto LCD_BL_LEDC_DUTY_RES = LEDC_TIMER_10_BIT;
constexpr auto LCD_BL_LEDC_DUTY = 1024;
constexpr auto LCD_BL_LEDC_FREQUENCY = 10000;

void setBacklightDuty(uint8_t level) {
    uint32_t duty = (level * (LCD_BL_LEDC_DUTY - 1)) / 255;
    ESP_ERROR_CHECK(ledc_set_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL));
}

void initBacklight() {
    ledc_timer_config_t timer_config = {
        .speed_mode = LCD_BL_LEDC_MODE,
        .duty_resolution = LCD_BL_LEDC_DUTY_RES,
        .timer_num = LCD_BL_LEDC_TIMER,
        .freq_hz = LCD_BL_LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    ledc_channel_config_t channel_config = {
        .gpio_num = GPIO_NUM_46,
        .speed_mode = LCD_BL_LEDC_MODE,
        .channel = LCD_BL_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LCD_BL_LEDC_TIMER,
        .duty = 0, // Set duty to 0%
        .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
}

bool initBoot() {
    LOG_I(TAG, LOG_MESSAGE_POWER_ON_START);
    initBacklight();
    return true;
}
