// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <tactility/drivers/gpio.h>

struct M5stackCardputerKeyboardConfig {
    struct GpioPinSpec* pins_output;
    uint8_t pins_output_count;
    struct GpioPinSpec* pins_input;
    uint8_t pins_input_count;
};

#ifdef __cplusplus
}
#endif
