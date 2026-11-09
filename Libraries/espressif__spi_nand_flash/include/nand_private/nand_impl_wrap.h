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

// These APIs provide direct access to lower-level NAND functions, bypassing the Dhara library.
// These functions differ from the similarly named `nand_*` functions in that they also take the mutex for the duration of the call.

esp_err_t nand_wrap_is_bad(spi_nand_flash_device_t *handle, uint32_t b, bool *is_bad_status);
esp_err_t nand_wrap_mark_bad(spi_nand_flash_device_t *handle, uint32_t b);
esp_err_t nand_wrap_erase_chip(spi_nand_flash_device_t *handle);
esp_err_t nand_wrap_erase_block(spi_nand_flash_device_t *handle, uint32_t b);
esp_err_t nand_wrap_prog(spi_nand_flash_device_t *handle, uint32_t p, const uint8_t *data);
esp_err_t nand_wrap_is_free(spi_nand_flash_device_t *handle, uint32_t p, bool *is_free_status);
esp_err_t nand_wrap_read(spi_nand_flash_device_t *handle, uint32_t p, size_t offset, size_t length, uint8_t *data);
esp_err_t nand_wrap_copy(spi_nand_flash_device_t *handle, uint32_t src, uint32_t dst);
esp_err_t nand_wrap_get_ecc_status(spi_nand_flash_device_t *handle, uint32_t page);

#ifdef __cplusplus
}
#endif
