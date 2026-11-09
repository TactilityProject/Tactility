// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/drivers/gpio.h>

struct ButtonControlConfig {
    struct GpioPinSpec pin_primary;
    // GPIO_PIN_SPEC_NONE selects one-button mode; see the binding description for the
    // resulting short/long press key mapping in each mode.
    struct GpioPinSpec pin_secondary;
    bool active_low;
    uint32_t debounce_ms;
    uint32_t long_press_ms;
    // GPIO_PIN_SPEC_NONE when there is no separate button for the previous item
    struct GpioPinSpec pin_previous;
    // GPIO_PIN_SPEC_NONE when there is no back button
    struct GpioPinSpec pin_back;
};

#ifdef __cplusplus
}
#endif
