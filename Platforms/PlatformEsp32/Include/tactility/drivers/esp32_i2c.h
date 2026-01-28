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
    gpio_pin_t pinSda;
    gpio_pin_t pinScl;
    bool pinSdaPullUp;
    bool pinSclPullUp;
};

error_t esp32_i2c_get_port(struct Device* device, i2c_port_t* port);
void esp32_i2c_lock(struct Device* device);
void esp32_i2c_unlock(struct Device* device);

#ifdef __cplusplus
}
#endif
