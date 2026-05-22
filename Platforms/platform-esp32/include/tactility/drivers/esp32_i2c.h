// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/gpio.h>
#include <hal/i2c_types.h>
#include <driver/i2c_master.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32I2cConfig {
    i2c_port_t port;
    uint32_t clockFrequency;
    struct GpioPinSpec pinSda;
    struct GpioPinSpec pinScl;
};

struct Device;

i2c_master_bus_handle_t esp32_i2c_get_bus_handle(struct Device* device);

#ifdef __cplusplus
}
#endif
