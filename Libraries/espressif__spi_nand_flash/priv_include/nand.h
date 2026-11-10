/*
 * SPDX-FileCopyrightText: 2022 mikkeldamsgaard project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPDX-FileContributor: 2015-2024 Espressif Systems (Shanghai) CO LTD
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "spi_nand_flash.h"
#ifdef CONFIG_IDF_TARGET_LINUX
#include "nand_linux_mmap_emul.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nand_device_types.h"

#ifdef CONFIG_NAND_FLASH_ENABLE_BDL
#include "esp_blockdev.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define INVALID_PAGE 0xFFFF

// True hardware dual-plane devices (e.g. Micron MT29F2G): plane index is encoded
// in the column address. See get_column_address() in nand_impl.c.
#define NAND_FLAG_HAS_PROG_PLANE_SELECT       BIT(0)
#define NAND_FLAG_HAS_READ_PLANE_SELECT       BIT(1)
// Single-plane devices whose Internal Data Move requires same odd/even block
// parity (e.g. some GigaDevice parts). nand_copy() uses a RAM path when parity differs.
#define NAND_FLAG_IDM_SAME_PARITY_REQUIRED    BIT(2)

// Legacy typedef for compatibility - now uses nand_flash_geometry_t internally
typedef nand_flash_geometry_t spi_nand_chip_t;

typedef struct {
    esp_err_t (*init)(spi_nand_flash_device_t *handle, void *bdl_handle); //if CONFIG_NAND_FLASH_ENABLE_BDL disabled, bdl_handle should be NULL
    esp_err_t (*deinit)(spi_nand_flash_device_t *handle);
    esp_err_t (*read)(spi_nand_flash_device_t *handle, uint8_t *buffer, uint32_t sector_id);
    esp_err_t (*write)(spi_nand_flash_device_t *handle, const uint8_t *buffer, uint32_t sector_id);
    esp_err_t (*erase_chip)(spi_nand_flash_device_t *handle);
    esp_err_t (*erase_block)(spi_nand_flash_device_t *handle, uint32_t block);
    esp_err_t (*trim)(spi_nand_flash_device_t *handle, uint32_t sector_id);
    esp_err_t (*sync)(spi_nand_flash_device_t *handle);
    esp_err_t (*copy_sector)(spi_nand_flash_device_t *handle, uint32_t src_sec, uint32_t dst_sec);
    esp_err_t (*get_capacity)(spi_nand_flash_device_t *handle, uint32_t *number_of_sectors);
    esp_err_t (*gc)(spi_nand_flash_device_t *handle);
} spi_nand_ops;

struct spi_nand_flash_device_t {
    spi_nand_flash_config_t config;
    spi_nand_chip_t chip;                  // Geometry (legacy typedef for nand_flash_geometry_t)
    nand_device_info_t device_info;        // Device identification (manufacturer, device ID, chip name)
    const spi_nand_ops *ops;
    void *ops_priv_data;
    uint8_t *work_buffer;
    uint8_t *read_buffer;
    uint8_t *temp_buffer;
    SemaphoreHandle_t mutex;
#ifdef CONFIG_IDF_TARGET_LINUX
    nand_mmap_emul_handle_t *emul_handle;
#endif
};

/** @return true if corrected-bit ECC class meets or exceeds the data-refresh threshold */
static inline bool nand_ecc_exceeds_data_refresh_threshold(const spi_nand_flash_device_t *handle)
{
    uint8_t min_bits_corrected = 0;
    if (handle->chip.ecc_data.ecc_corrected_bits_status == NAND_ECC_1_TO_3_BITS_CORRECTED) {
        min_bits_corrected = 1;
    } else if (handle->chip.ecc_data.ecc_corrected_bits_status == NAND_ECC_4_TO_6_BITS_CORRECTED) {
        min_bits_corrected = 4;
    } else if (handle->chip.ecc_data.ecc_corrected_bits_status == NAND_ECC_7_8_BITS_CORRECTED) {
        min_bits_corrected = 7;
    }
    return min_bits_corrected >= handle->chip.ecc_data.ecc_data_refresh_threshold;
}

/**
 * @brief Attach wear-leveling operations to NAND device (internal use only)
 *
 * This function attaches the Dhara wear-leveling operation callbacks to the
 * device, enabling wear-leveling functionality.
 *
 * @param[in] handle  NAND device handle
 *
 * @return
 *         - ESP_OK: Success
 *         - ESP_ERR_INVALID_ARG: Invalid handle
 */
esp_err_t nand_wl_attach_ops(spi_nand_flash_device_t *handle);

/**
 * @brief Detach wear-leveling operations from NAND device (internal use only)
 *
 * This function detaches the wear-leveling operation callbacks and frees
 * associated private data.
 *
 * @param[in] handle  NAND device handle
 *
 * @return
 *         - ESP_OK: Success
 */
esp_err_t nand_wl_detach_ops(spi_nand_flash_device_t *handle);

#ifdef __cplusplus
}
#endif
