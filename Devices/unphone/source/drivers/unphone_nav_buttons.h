// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/drivers/gpio.h>

struct UnphoneNavButtonsConfig {
    struct GpioPinSpec pin_button1;
    struct GpioPinSpec pin_button2;
    struct GpioPinSpec pin_button3;
};

#ifdef __cplusplus
}
#endif
