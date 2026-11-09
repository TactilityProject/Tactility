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
};

#ifdef __cplusplus
}
#endif
