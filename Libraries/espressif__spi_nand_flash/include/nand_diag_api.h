/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "spi_nand_flash.h"

#ifdef __cplusplus
extern "C" {
#endif

// These API used for diagnostic purpose of SPI NAND Flash

/** @brief Get bad block statistics for the NAND Flash.
 *
 * This function scans all the blocks in the NAND Flash and returns the total count of bad blocks.
 *
 * @param flash The handle to the SPI nand flash chip.
 * @param[out] bad_block_count A pointer of where to put the return value
 * @return ESP_OK on success, or a flash error code if it fails to get bad block statistics.
 */
esp_err_t nand_get_bad_block_stats(spi_nand_flash_device_t *flash, uint32_t *bad_block_count);

/** @brief Get ECC error statistics for the NAND Flash.
 *
 * This function displays the total ECC errors reported, ECC not corrected error count and ECC error count exceeding threshold.
 *
 * @param flash The handle to the SPI nand flash chip.
 * @return ESP_OK on success, or a flash error code if it failed to read the page.
 */
esp_err_t nand_get_ecc_stats(spi_nand_flash_device_t *flash);

#ifdef __cplusplus
}
#endif
