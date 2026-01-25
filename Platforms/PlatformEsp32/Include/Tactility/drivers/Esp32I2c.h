// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <Tactility/drivers/Gpio.h>
#include <hal/i2c_types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32I2cConfig {
    uint32_t clockFrequency;
    struct GpioPinConfig pinSda;
    struct GpioPinConfig pinScl;
    const i2c_port_t port;
};

#ifdef __cplusplus
}
#endif
