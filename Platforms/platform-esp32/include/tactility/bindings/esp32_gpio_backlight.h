// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <tactility/drivers/esp32_gpio_backlight.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_gpio_backlight, struct Esp32GpioBacklightConfig)

#ifdef __cplusplus
}
#endif
