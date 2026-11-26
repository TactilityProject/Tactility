// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#include <tactility/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct GpioAntennaSwitchConfig {
    struct GpioPinSpec* gpios;
    uint8_t gpios_count;
    uint32_t default_antenna;
};

#ifdef __cplusplus
}
#endif
