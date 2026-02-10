// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <tactility/bindings/gpio.h>
#include <tactility/drivers/esp32_gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPIO_ACTIVE_HIGH GPIO_FLAG_ACTIVE_HIGH
#define GPIO_ACTIVE_LOW GPIO_FLAG_ACTIVE_LOW

#define GPIO_DIRECTION_INPUT GPIO_FLAG_DIRECTION_INPUT
#define GPIO_DIRECTION_OUTPUT GPIO_FLAG_DIRECTION_OUTPUT
#define GPIO_DIRECTION_INPUT_OUTPUT GPIO_FLAG_DIRECTION_INPUT_OUTPUT

#define GPIO_PULL_UP GPIO_FLAG_PULL_UP
#define GPIO_PULL_DOWN GPIO_FLAG_PULL_DOWN

DEFINE_DEVICETREE(esp32_gpio, struct Esp32GpioConfig)

#ifdef __cplusplus
}
#endif
