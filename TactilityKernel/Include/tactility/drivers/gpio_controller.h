// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "gpio.h"
#include <tactility/error.h>

struct GpioControllerApi {
    error_t (*set_level)(struct Device* device, gpio_pin_t pin, bool high);
    error_t (*get_level)(struct Device* device, gpio_pin_t pin, bool* high);
    error_t (*set_options)(struct Device* device, gpio_pin_t pin, gpio_flags_t options);
    error_t (*get_options)(struct Device* device, gpio_pin_t pin, gpio_flags_t* options);
};

error_t gpio_controller_set_level(struct Device* device, gpio_pin_t pin, bool high);
error_t gpio_controller_get_level(struct Device* device, gpio_pin_t pin, bool* high);
error_t gpio_controller_set_options(struct Device* device, gpio_pin_t pin, gpio_flags_t options);
error_t gpio_controller_get_options(struct Device* device, gpio_pin_t pin, gpio_flags_t* options);

static inline error_t gpio_set_options_config(struct Device* device, const struct GpioPinConfig* config) {
    return gpio_controller_set_options(device, config->pin, config->flags);
}

#ifdef __cplusplus
}
#endif
