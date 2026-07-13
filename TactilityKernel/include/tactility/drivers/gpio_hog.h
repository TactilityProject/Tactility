// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include <tactility/drivers/gpio.h>

struct GpioHogConfig {
    struct GpioPinSpec pin;
    bool output_high;
};

#ifdef __cplusplus
}
#endif
