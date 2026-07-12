// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/drivers/gpio.h>

struct Cst816sConfig {
    // Devicetree address hint. Unused by the driver: the CST816S always sits at a fixed
    // I2C address (0x15, see ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS), unlike GT911's
    // strapping-dependent address.
    uint8_t address;
    uint16_t x_max;
    uint16_t y_max;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    struct GpioPinSpec pin_reset;
    struct GpioPinSpec pin_interrupt;
    bool reset_active_high;
    bool interrupt_active_high;
};

#ifdef __cplusplus
}
#endif
