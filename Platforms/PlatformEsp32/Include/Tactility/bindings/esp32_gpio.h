// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <Tactility/bindings/bindings.h>
#include <Tactility/drivers/Esp32Gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_gpio, struct Esp32GpioConfig)

#ifdef __cplusplus
}
#endif
