// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/gpio.h>
#include <driver/i2c_types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32I2cMasterConfig {
    i2c_port_num_t port;
    uint32_t clockFrequency;
    int32_t clkSource;
    struct GpioPinSpec pinSda;
    struct GpioPinSpec pinScl;
};

#ifdef __cplusplus
}
#endif
