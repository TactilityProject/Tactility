// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

struct Ssd1306Config {
    // Devicetree address hint - unlike this repo's other I2C touch chips (which sit at a fixed
    // address), the SSD1306's address genuinely varies by hardware strap (0x3C or 0x3D), so this
    // is actually used, not just documentation.
    uint8_t address;
    uint16_t horizontal_resolution;
    uint16_t vertical_resolution;
    bool invert_color;

    // Reset pin. GPIO_PIN_SPEC_NONE skips the reset toggle entirely.
    struct GpioPinSpec pin_reset;
    bool reset_active_high;

    // See the 'power-on-delay-ms' binding property.
    uint32_t power_on_delay_ms;

    // Custom vendor init sequence: a flat list of command bytes, each sent as its own
    // single-byte SSD1306 command. NULL/0 falls back to the standard esp_lcd_panel_init().
    const uint8_t* init_sequence;
    uint32_t init_sequence_length;
};

#ifdef __cplusplus
}
#endif
