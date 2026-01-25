// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "Gpio.h"
#include <stdbool.h>

struct GpioControllerApi {
    int (*set_level)(struct Device* device, gpio_pin_t pin, bool high);
    int (*get_level)(struct Device* device, gpio_pin_t pin, bool* high);
    int (*set_options)(struct Device* device, gpio_pin_t pin, gpio_flags_t options);
    int (*get_options)(struct Device* device, gpio_pin_t pin, gpio_flags_t* options);
};

int gpio_controller_set_level(struct Device* device, gpio_pin_t pin, bool high);
int gpio_controller_get_level(struct Device* device, gpio_pin_t pin, bool* high);
int gpio_controller_set_options(struct Device* device, gpio_pin_t pin, gpio_flags_t options);
int gpio_controller_get_options(struct Device* device, gpio_pin_t pin, gpio_flags_t* options);

static inline bool gpio_set_options_config(struct Device* device, const struct GpioPinConfig* config) {
    return gpio_controller_set_options(device, config->pin, config->flags);
}

#ifdef __cplusplus
}
#endif
