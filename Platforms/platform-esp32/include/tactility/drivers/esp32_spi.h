// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <driver/spi_common.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32SpiConfig {
    spi_host_device_t host;
    /** Clock pin */
    struct GpioPinSpec pin_sclk;
    /** Data 0 pin */
    struct GpioPinSpec pin_mosi;
    /** Data 1 pin */
    struct GpioPinSpec pin_miso;
    /** Data 2 pin */
    struct GpioPinSpec pin_wp;
    /** Data 3 pin */
    struct GpioPinSpec pin_hd;
    /** Data transfer size limit in bytes. 0 means the platform decides the limit. */
    int max_transfer_size;
    /** Array of chip select GPIO pin specs */
    struct GpioPinSpec* cs_gpios;
    /** The item count of cs_gpios */
    uint8_t cs_gpios_count;
    /**
     * Enables a weak internal pull-up on MISO, which floats between transactions/while
     * another device on the bus is selected. Helps some SPI peripherals (a garbled/invalid
     * response on CMD8/if_cond has been observed for SD-over-SPI on some boards) but has been
     * observed to break others (e.g. prevents the SD card from responding to CMD0 at all on
     * some boards) - opt in per board rather than defaulting it on for everyone.
     */
    bool miso_pull_up;
};

/**
 * @brief Get the CS pin spec for a child device on this SPI bus.
 * Uses the child device's address as index into the parent's cs_gpios array.
 * @param[in] child_device a child device of an SPI controller
 * @param[out] out_pin the GPIO pin spec for the CS pin
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_STATE if the parent is not an SPI controller
 * @retval ERROR_OUT_OF_RANGE if the device address exceeds the cs_gpios array
 */
error_t esp32_spi_get_cs_pin(struct Device* child_device, struct GpioPinSpec* out_pin);

/**
 * @brief Drive all CS pins on this SPI bus high (deselected).
 * @param[in] device the SPI controller device
 */
void esp32_spi_deselect_all_cs(struct Device* device);

#ifdef __cplusplus
}
#endif
