// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <tactility/drivers/esp32_adc_oneshot.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_adc_oneshot, struct Esp32AdcOneshotConfig)

#ifdef __cplusplus
}
#endif
