// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/drivers/gpio.h>

struct GpioButtonConfig {
    struct GpioPinSpec pin;
    uint32_t short_press_code;
    uint32_t long_press_code;
    uint32_t long_press_ms;
    uint32_t debounce_ms;
};

#ifdef __cplusplus
}
#endif
