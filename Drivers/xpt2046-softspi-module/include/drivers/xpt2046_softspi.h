// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/drivers/gpio.h>

struct Xpt2046SoftSpiConfig {
    struct GpioPinSpec pin_mosi;
    struct GpioPinSpec pin_miso;
    struct GpioPinSpec pin_sck;
    struct GpioPinSpec pin_cs;
    uint16_t x_max;
    uint16_t y_max;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    /** Expose a power-supply child device that reads battery voltage/capacity off the chip's v-bat input */
    bool power_supply;
    /** Battery voltage (mV) considered 100% capacity, used to derive POWER_SUPPLY_PROP_CAPACITY */
    uint32_t power_supply_reference_voltage_mv;
};

#ifdef __cplusplus
}
#endif
