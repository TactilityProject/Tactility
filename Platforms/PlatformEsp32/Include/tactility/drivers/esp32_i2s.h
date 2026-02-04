// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/i2s_controller.h>
#include <driver/i2s_common.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32I2sConfig {
    i2s_port_t port;
    int pin_bclk;
    int pin_ws;
    int pin_data_out;
    int pin_data_in;
    int pin_mclk;
};

#ifdef __cplusplus
}
#endif
