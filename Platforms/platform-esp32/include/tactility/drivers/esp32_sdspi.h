// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sd_protocol_types.h>
#include <tactility/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32SdspiConfig {
    struct GpioPinSpec pin_cd;
    struct GpioPinSpec pin_wp;
    struct GpioPinSpec pin_int;
    uint32_t frequency_khz;
};

sdmmc_card_t* esp32_sdspi_get_card(struct Device* device);

#ifdef __cplusplus
}
#endif
