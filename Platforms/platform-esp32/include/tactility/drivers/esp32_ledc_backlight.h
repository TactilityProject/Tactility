// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <driver/ledc.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32LedcBacklightConfig {
    struct GpioPinSpec pin_backlight;
    uint32_t frequency_hz;
    struct BrightnessLevelRange brightness_range;
    uint8_t brightness_default;
    ledc_timer_t ledc_timer;
    ledc_channel_t ledc_channel;
};

#ifdef __cplusplus
}
#endif
