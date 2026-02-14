// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/gpio.h>
#include <hal/i2c_types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32I2cConfig {
    i2c_port_t port;
    uint32_t clockFrequency;
    struct GpioPinSpec pinSda;
    struct GpioPinSpec pinScl;
};

#ifdef __cplusplus
}
#endif
