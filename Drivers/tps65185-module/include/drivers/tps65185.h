// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tactility/drivers/gpio.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Tps65185Config {
    /** Address on bus */
    uint8_t address;
    /** Magnitude of the VCOM voltage in millivolts */
    uint32_t vcom_millivolt;
    /** WAKEUP control pin */
    struct GpioPinSpec pin_wakeup;
    /** PWRUP control pin */
    struct GpioPinSpec pin_pwrup;
    /** VCOM_CTRL control pin */
    struct GpioPinSpec pin_vcom_ctrl;
    /** PWR_GOOD status pin */
    struct GpioPinSpec pin_power_good;
};

/**
 * Brings up the rails and programs VCOM.
 * @param[in] device tps65185 device
 * @retval ERROR_NONE when all rails report power good
 * @retval ERROR_TIMEOUT when a power good signal did not arrive
 */
error_t tps65185_on(struct Device* device);

/** Starts the discharge steps and disables VCOM. Does nothing when the rails are off. */
void tps65185_off(struct Device* device);

/**
 * Programs VCOM while the rails are on.
 * @param[in] device tps65185 device
 * @param[in] millivolt magnitude of the VCOM voltage, in 10 mV steps up to 5110 mV
 * @retval ERROR_INVALID_ARGUMENT when the voltage is out of range
 */
error_t tps65185_set_vcom(struct Device* device, uint32_t millivolt);

#ifdef __cplusplus
}
#endif
