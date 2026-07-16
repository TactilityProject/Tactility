// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/drivers/gpio.h>

enum GpioHogMode {
    GPIO_HOG_MODE_OUTPUT_HIGH,
    GPIO_HOG_MODE_OUTPUT_LOW,
    GPIO_HOG_MODE_INPUT,
};

struct GpioHogConfig {
    struct GpioPinSpec pin;
    enum GpioHogMode mode;
};

#ifdef __cplusplus
}
#endif
