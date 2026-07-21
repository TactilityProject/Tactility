// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/drivers/gpio.h>

struct Axs5106Config {
    uint8_t address;
    uint16_t x_max;
    uint16_t y_max;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    struct GpioPinSpec pin_reset;
    struct GpioPinSpec pin_interrupt;
    // Timeout (ms) for the register-address write half of a read, and for the data-read half,
    // done as two separate I2C transactions - matches the vendor factory demo's
    // i2c_master_transmit()/i2c_master_receive() split (each with its own 100ms timeout),
    // rather than a single combined write-then-read transaction.
    uint32_t transmit_timeout_ms;
    uint32_t receive_timeout_ms;
};

#ifdef __cplusplus
}
#endif
