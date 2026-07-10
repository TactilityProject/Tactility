// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <tactility/drivers/esp32_ledc_backlight.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_ledc_backlight, struct Esp32LedcBacklightConfig)

#ifdef __cplusplus
}
#endif
