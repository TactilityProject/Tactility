// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "Gpio.h"
#include <stdbool.h>

struct GpioControllerApi {
    bool (*set_level)(struct Device* device, gpio_pin_t pin, bool high);
    bool (*get_level)(struct Device* device, gpio_pin_t pin, bool* high);
    bool (*set_options)(struct Device* device, gpio_pin_t pin, gpio_flags_t options);
    bool (*get_options)(struct Device* device, gpio_pin_t pin, gpio_flags_t* options);
};

bool gpio_controller_set_level(struct Device* device, gpio_pin_t pin, bool high);
bool gpio_controller_get_level(struct Device* device, gpio_pin_t pin, bool* high);
bool gpio_controller_set_options(struct Device* device, gpio_pin_t pin, gpio_flags_t options);
bool gpio_controller_get_options(struct Device* device, gpio_pin_t pin, gpio_flags_t* options);

inline bool gpio_set_options_config(struct Device* device, struct GpioPinConfig* config) {
    return gpio_controller_set_options(device, config->pin, config->dt_flags);
}

#ifdef __cplusplus
}
#endif
