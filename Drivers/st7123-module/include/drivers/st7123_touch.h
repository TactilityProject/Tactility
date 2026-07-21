// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/drivers/gpio.h>

struct St7123TouchConfig {
    // Devicetree address hint. esp_lcd_touch_st7123 always talks to the controller's one fixed
    // address (0x55 - see ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS), so this isn't actually used to
    // probe the bus - it just documents the wiring like any other i2c-device node.
    uint8_t address;
    uint16_t x_max;
    uint16_t y_max;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    // Reset is wired through the board's IO expander rather than a direct SoC GPIO on every
    // known user of this driver so far, hence GPIO_PIN_SPEC_NONE by default - but exposed here
    // for parity with the other touch drivers (e.g. gt911-module) in case a board wires it directly.
    struct GpioPinSpec pin_reset;
    struct GpioPinSpec pin_interrupt;
};

#ifdef __cplusplus
}
#endif
