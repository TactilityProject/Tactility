// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <soc/soc_caps.h>
#if SOC_SDMMC_HOST_SUPPORTED

#include <sd_protocol_types.h>
#include <stdbool.h>
#include <stdint.h>
#include <tactility/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32SdmmcConfig {
    struct GpioPinSpec pin_clk;
    struct GpioPinSpec pin_cmd;
    struct GpioPinSpec pin_d0;
    struct GpioPinSpec pin_d1;
    struct GpioPinSpec pin_d2;
    struct GpioPinSpec pin_d3;
    struct GpioPinSpec pin_d4;
    struct GpioPinSpec pin_d5;
    struct GpioPinSpec pin_d6;
    struct GpioPinSpec pin_d7;
    struct GpioPinSpec pin_cd;
    struct GpioPinSpec pin_wp;
    uint8_t bus_width;
    bool wp_active_high;
    bool enable_uhs;
    bool pullups;
    int32_t on_chip_ldo_chan;
};

/**
 * @brief Get the SD card handle for the given device.
 * @param[in] device the device to get the card handle for
 * @return the SD card handle, or NULL if the device is not mounted
 */
sdmmc_card_t* esp32_sdmmc_get_card(struct Device* device);

#ifdef __cplusplus
}
#endif

#endif