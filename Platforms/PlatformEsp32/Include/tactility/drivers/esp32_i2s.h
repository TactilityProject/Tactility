// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/i2s_controller.h>
#include <tactility/drivers/gpio.h>
#include <driver/i2s_common.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32I2sConfig {
    i2s_port_t port;
    gpio_pin_t pin_bclk;
    gpio_pin_t pin_ws;
    gpio_pin_t pin_data_out;
    gpio_pin_t pin_data_in;
    gpio_pin_t pin_mclk;
};

#ifdef __cplusplus
}
#endif
