// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <tactility/drivers/esp32_pwm_ledc.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_pwm_ledc, struct Esp32PwmLedcConfig)

#ifdef __cplusplus
}
#endif
