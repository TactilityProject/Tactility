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
    struct GpioPinSpec pin_bclk;
    struct GpioPinSpec pin_ws;
    struct GpioPinSpec pin_data_out;
    struct GpioPinSpec pin_data_in;
    struct GpioPinSpec pin_mclk;
};

#ifdef __cplusplus
}
#endif
