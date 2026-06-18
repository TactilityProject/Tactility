// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/gpio.h>
#include <driver/i2c_master.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;

struct Esp32I2cMasterConfig {
    i2c_port_num_t port;
    uint32_t clockFrequency;
    int32_t clkSource;
    struct GpioPinSpec pinSda;
    struct GpioPinSpec pinScl;
};

i2c_master_bus_handle_t esp32_i2c_master_get_bus_handle(struct Device* device);

#ifdef __cplusplus
}
#endif
