// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/drivers/gpio.h>

struct Ft6x36Config {
    // Devicetree address hint. Unused by the driver: the FT6x36 always sits at a fixed
    // I2C address (0x38, see ESP_LCD_TOUCH_IO_I2C_FT6x36_ADDRESS).
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
