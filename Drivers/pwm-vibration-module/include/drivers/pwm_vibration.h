// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#include <tactility/device.h>
#include <tactility/drivers/vibration.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PwmVibrationConfig {
    /** The PWM device driving the motor */
    struct Device* pwm;
    /** Default strength percentage (0..100) */
    uint8_t strength_default;
};

#ifdef __cplusplus
}
#endif
