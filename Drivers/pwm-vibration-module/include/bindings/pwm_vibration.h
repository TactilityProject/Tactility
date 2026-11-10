// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/pwm_vibration.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(pwm_vibration, struct PwmVibrationConfig)

#ifdef __cplusplus
}
#endif
