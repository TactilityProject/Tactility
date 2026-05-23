// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Ina226Config {
    uint8_t address;
    uint16_t shunt_milliohms;
};

/** Bus voltage in volts (1.25 mV/LSB) */
error_t ina226_read_bus_voltage(struct Device* device, float* volts);

/** Shunt current in amps (positive = charging, negative = discharging) */
error_t ina226_read_shunt_current(struct Device* device, float* amps);

#ifdef __cplusplus
}
#endif
