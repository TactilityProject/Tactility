/*
 * SPDX-FileCopyrightText: 2022 mikkeldamsgaard project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPDX-FileContributor: 2015-2023 Espressif Systems (Shanghai) CO LTD
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "nand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize NAND flash device (internal use only)
 *
 * This is an internal function that initializes the NAND flash hardware,
 * detects the chip, and creates the device structure. It does NOT create
 * any block device interface.
 *
 * Used by:
 * - nand_flash_get_blockdev() - to create Flash BDL
 * - spi_nand_flash_init_device() - for legacy API
 *
 * @param[in]  config  Configuration for the SPI NAND flash device
 * @param[out] handle  Pointer to store the created NAND device handle
 *
 * @return
 *         - ESP_OK: Success
 *         - ESP_ERR_INVALID_ARG: Invalid configuration or NULL pointer
 *         - ESP_ERR_NO_MEM: Insufficient memory
 *         - ESP_ERR_NOT_FOUND: NAND device not detected
 *
 * @note This is INTERNAL API. Do not use directly in applications.
 */
esp_err_t nand_init_device(spi_nand_flash_config_t *config,
                           spi_nand_flash_device_t **handle);

esp_err_t nand_is_bad(spi_nand_flash_device_t *handle, uint32_t b, bool *is_bad_status);
esp_err_t nand_mark_bad(spi_nand_flash_device_t *handle, uint32_t b);
esp_err_t nand_erase_chip(spi_nand_flash_device_t *handle);
esp_err_t nand_erase_block(spi_nand_flash_device_t *handle, uint32_t b);
esp_err_t nand_prog(spi_nand_flash_device_t *handle, uint32_t p, const uint8_t *data);
esp_err_t nand_is_free(spi_nand_flash_device_t *handle, uint32_t p, bool *is_free_status);
esp_err_t nand_read(spi_nand_flash_device_t *handle, uint32_t p, size_t offset, size_t length, uint8_t *data);
esp_err_t nand_copy(spi_nand_flash_device_t *handle, uint32_t src, uint32_t dst);
esp_err_t nand_get_ecc_status(spi_nand_flash_device_t *handle, uint32_t page);

#ifdef __cplusplus
}
#endif
