// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <tactility/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Field order must match the property order in bindings/semtech,sx1262.yaml */
struct Sx1262Config {
    /** Chip-select index on the parent SPI controller, must match the node's unit address */
    int32_t reg;
    /** SPI clock frequency in kHz */
    uint32_t spi_frequency_khz;
    /** NRESET line (must be an SoC GPIO) */
    struct GpioPinSpec pin_reset;
    /** BUSY line (must be an SoC GPIO) */
    struct GpioPinSpec pin_busy;
    /** DIO1 interrupt line (must be an SoC GPIO) */
    struct GpioPinSpec pin_dio1;
    /** Optional module power enable, driven active while the device is started */
    struct GpioPinSpec pin_enable;
    /** Optional antenna select, driven active while the device is started */
    struct GpioPinSpec pin_antenna_select;
    /** TCXO reference voltage on DIO3 in millivolts, 0 when no TCXO is fitted */
    uint32_t tcxo_millivolts;
    /** Use the LDO regulator instead of the DC-DC converter */
    bool use_regulator_ldo;
    /** DIO2 drives the antenna TX/RX switch */
    bool dio2_as_rf_switch;
};

#ifdef __cplusplus
}
#endif
