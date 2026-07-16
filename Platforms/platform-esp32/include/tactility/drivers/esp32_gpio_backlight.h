// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/backlight.h>
#include <tactility/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32GpioBacklightConfig {
    struct GpioPinSpec pin_backlight;
    bool default_on;
};

#ifdef __cplusplus
}
#endif
