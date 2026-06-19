// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/gpio.h>
#include <tactility/device.h>
#include <driver/i2c_types.h>
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

/**
 * Returns the i2c_master_bus_handle_t for an esp32_i2c_master Device.
 * The device must be started. Returns NULL if the device has no driver data.
 */
i2c_master_bus_handle_t esp32_i2c_master_get_bus_handle(struct Device* device);

/**
 * Returns the SCL clock frequency (Hz) configured for an esp32_i2c_master Device.
 * Reads directly from the device tree config, so the device does not need to be started.
 */
uint32_t esp32_i2c_master_get_clock_frequency(struct Device* device);

#ifdef __cplusplus
}
#endif
