// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <driver/ledc.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/pwm.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32PwmLedcConfig {
    struct GpioPinSpec pin;
    uint32_t period_ns;
    uint32_t duty_ns;
    bool inverted;
    ledc_timer_bit_t duty_resolution;
    ledc_timer_t ledc_timer;
    ledc_channel_t ledc_channel;
};

#ifdef __cplusplus
}
#endif
