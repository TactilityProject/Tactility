// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <soc/soc_caps.h>
#if SOC_LCD_I80_SUPPORTED

#include <tactility/drivers/i8080_controller.h>
#include <tactility/drivers/gpio.h>

#include <esp_lcd_io_i80.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32I8080Config {
    struct GpioPinSpec pin_dc;
    struct GpioPinSpec pin_wr;
    struct GpioPinSpec pin_rd;
    struct GpioPinSpec pin_d0;
    struct GpioPinSpec pin_d1;
    struct GpioPinSpec pin_d2;
    struct GpioPinSpec pin_d3;
    struct GpioPinSpec pin_d4;
    struct GpioPinSpec pin_d5;
    struct GpioPinSpec pin_d6;
    struct GpioPinSpec pin_d7;
    int max_transfer_bytes;
    /** Array of chip select GPIO pin specs */
    struct GpioPinSpec* cs_gpios;
    /** The item count of cs_gpios */
    uint8_t cs_gpios_count;
};

/**
 * @brief Get the CS pin spec for a child device on this i8080 bus.
 * Uses the child device's address as index into the parent's cs_gpios array.
 * @param[in] child_device a child device of an i8080 controller
 * @param[out] out_pin the GPIO pin spec for the CS pin
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_STATE if the parent is not an i8080 controller
 * @retval ERROR_OUT_OF_RANGE if the device address exceeds the cs_gpios array
 */
error_t esp32_i8080_get_cs_pin(struct Device* child_device, struct GpioPinSpec* out_pin);

/**
 * @brief Get the i80 bus handle for a child device's i8080 controller parent.
 * Only valid after the controller device has started.
 * @param[in] child_device a child device of an i8080 controller
 * @param[out] out_handle the bus handle
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_STATE if the parent is not an i8080 controller, or hasn't started
 */
error_t esp32_i8080_get_bus_handle(struct Device* child_device, esp_lcd_i80_bus_handle_t* out_handle);

#ifdef __cplusplus
}
#endif

#endif // SOC_LCD_I80_SUPPORTED
