// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/error.h>

/** Acquires a pin as input */
static inline error_t tps65185_gpio_acquire_input(const GpioPinSpec& pin, GpioDescriptor** out_descriptor) {
    *out_descriptor = gpio_descriptor_acquire(pin.gpio_controller, pin.pin, pin.flags | GPIO_FLAG_DIRECTION_INPUT, GPIO_OWNER_GPIO);
    return (*out_descriptor != nullptr) ? ERROR_NONE : ERROR_RESOURCE;
}

/** Acquires a pin as output with a known initial level */
static inline error_t tps65185_gpio_acquire_output(const GpioPinSpec& pin, bool initial_level, GpioDescriptor** out_descriptor) {
    error_t error = tps65185_gpio_acquire_input(pin, out_descriptor);
    if (error != ERROR_NONE) return error;

    error = gpio_descriptor_set_level(*out_descriptor, initial_level);
    if (error == ERROR_NONE) {
        error = gpio_descriptor_set_flags(*out_descriptor, pin.flags | GPIO_FLAG_DIRECTION_OUTPUT);
    }
    if (error != ERROR_NONE) {
        gpio_descriptor_release(*out_descriptor);
        *out_descriptor = nullptr;
    }
    return error;
}
