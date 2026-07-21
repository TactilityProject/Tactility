// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sd_protocol_types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;

/**
 * Try to get the sdmmc_card_t* for the given device.
 * @param device any device.
 * @return the sdmmc_card_t* if it is available for this device, otherwise return null
 */
sdmmc_card_t* esp32_sdcard_get_card(struct Device* device);

#ifdef __cplusplus
}
#endif
