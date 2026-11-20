// SPDX-License-Identifier: Apache-2.0
#include "gdep103tc2_power.h"

static void release(GpioDescriptor** descriptor) {
    if (*descriptor != nullptr) {
        gpio_descriptor_release(*descriptor);
        *descriptor = nullptr;
    }
}

error_t gdep103tc2_power_init(Gdep103tc2Power* power, const Gdep103tc2Config* config) {
    if (config->pin_power_enable.gpio_controller != nullptr) {
        error_t error = gdep103tc2_gpio_acquire_output(config->pin_power_enable, false, &power->enable);
        if (error != ERROR_NONE) return error;
    }

    error_t error = gdep103tc2_gpio_acquire_output(config->pin_oe, false, &power->oe);
    if (error == ERROR_NONE) error = gdep103tc2_gpio_acquire_output(config->pin_gmod, false, &power->gmod);
    if (error != ERROR_NONE) {
        release(&power->enable);
        release(&power->oe);
        release(&power->gmod);
    }
    return error;
}

void gdep103tc2_power_deinit(Gdep103tc2Power* power) {
    gdep103tc2_power_off(power);
    release(&power->enable);
    release(&power->oe);
    release(&power->gmod);
}

error_t gdep103tc2_power_on(Gdep103tc2Power* power) {
    if (power->on) return ERROR_NONE;

    power->on = true;
    error_t error = ERROR_NONE;
    if (power->enable != nullptr) error = gpio_descriptor_set_level(power->enable, true);
    if (error == ERROR_NONE) error = gpio_descriptor_set_level(power->oe, true);
    if (error == ERROR_NONE) error = gpio_descriptor_set_level(power->gmod, true);
    if (error != ERROR_NONE) {
        gdep103tc2_power_off(power);
        return error;
    }
    return ERROR_NONE;
}

void gdep103tc2_power_off(Gdep103tc2Power* power) {
    if (!power->on) return;
    gpio_descriptor_set_level(power->gmod, false);
    gpio_descriptor_set_level(power->oe, false);
    if (power->enable != nullptr) gpio_descriptor_set_level(power->enable, false);
    power->on = false;
}
