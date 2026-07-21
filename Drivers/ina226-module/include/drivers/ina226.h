// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Ina226Config {
    uint8_t address;
    uint16_t shunt_milliohms;
    /** Expose a power-supply child device that reports battery voltage/capacity off the bus voltage reading */
    bool power_supply;
    /** Battery voltage (mV) considered 100% capacity, used to derive POWER_SUPPLY_PROP_CAPACITY */
    uint32_t power_supply_reference_voltage_mv;
    /** Battery voltage (mV) considered 0% capacity, used to derive POWER_SUPPLY_PROP_CAPACITY */
    uint32_t power_supply_min_voltage_mv;
};

/** Bus voltage in volts (1.25 mV/LSB) */
error_t ina226_read_bus_voltage(struct Device* device, float* volts);

/** Shunt current in amps (positive = charging, negative = discharging) */
error_t ina226_read_shunt_current(struct Device* device, float* amps);

#ifdef __cplusplus
}
#endif
