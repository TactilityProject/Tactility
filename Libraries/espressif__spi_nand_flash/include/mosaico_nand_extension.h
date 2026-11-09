// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdint.h>
#include "esp_err.h"
#ifndef CONFIG_IDF_TARGET_LINUX
#include "driver/spi_master.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Local Mosaico fork additions; not part of the upstream 1.4.4 interface. */
#define MOSAICO_NAND_DISCARD_ALL 0x95U
#define MOSAICO_NAND_DISCARD_CONFIRM 0x4e414e44U
struct spi_nand_flash_device_t;

/* Destructive format primitive: the caller must quiesce all filesystem users.
 * Clears and syncs logical mappings; preserves physical factory bad markers.
 * BDL callers use DISCARD_ALL with the exact DISCARD_CONFIRM token. */
esp_err_t mosaico_nand_clear_map(struct spi_nand_flash_device_t *handle);
#ifndef CONFIG_IDF_TARGET_LINUX
/* On a single-IO bus with WP/HOLD high, enable/verify the detected chip's QE
 * policy before routing IO2/IO3. Does not initialize an FTL. */
esp_err_t mosaico_nand_prepare_quad(spi_device_handle_t spi);
#endif

#ifdef __cplusplus
}
#endif
