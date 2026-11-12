/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Control structure for NAND emulation
typedef struct {
    char flash_file_name[256];
    /**
     * Size of the backing mmap file in bytes. Must be a multiple of the chip's
     * user-visible block size (page_size * pages_per_block).
     *
     * Note: the file on disk is slightly larger than the user-visible NAND capacity
     * because each page has OOB bytes interleaved after it. The actual number of
     * usable blocks is derived internally as:
     *   num_blocks = flash_file_size / (pages_per_block * (page_size + oob_size))
     * and the effective user-visible capacity is num_blocks * block_size, which
     * will be slightly less than flash_file_size due to the OOB overhead.
     *
     * Example: passing 8 MiB with 2 KiB pages, 64 B OOB, 64 pages/block gives
     *   file_bytes_per_block = 64 * (2048 + 64) = 135168 B
     *   num_blocks = 8388608 / 135168 = 62 blocks
     *   effective capacity = 62 * 131072 = 7.75 MiB (user-visible)
     */
    size_t flash_file_size;
    bool keep_dump;
} nand_file_mmap_emul_config_t;

// nand mmap emulator handle
typedef struct {
    void *mem_file_buf;
    int mem_file_fd;
    nand_file_mmap_emul_config_t file_mmap_ctrl;
#ifdef CONFIG_NAND_ENABLE_STATS
    struct {
        size_t read_ops;
        size_t write_ops;
        size_t erase_ops;
        size_t read_bytes;
        size_t write_bytes;
    } stats;
#endif
} nand_mmap_emul_handle_t;

// Emulated nand mmap file size
#define EMULATED_NAND_SIZE        128 * 1024 * 1024

#include "spi_nand_flash.h"

/**
 * @brief Initialize NAND flash emulation
 *
 * @param handle spi_nand_flash_device_t handle for nand device
 * @param cfg mmap emulation configuration setting
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_STATE if already initialized
 *         ESP_ERR_NOT_FOUND if backing file cannot be created or opened (named path or default temp file via mkstemp)
 *         ESP_ERR_INVALID_SIZE if file size setting fails
 *         ESP_ERR_NO_MEM if memory not available or mmap fails
 */
esp_err_t nand_emul_init(spi_nand_flash_device_t *handle, nand_file_mmap_emul_config_t *cfg);

/**
 * @brief Clean up NAND flash emulation
 *
 * @param handle spi_nand_flash_device_t handle for nand device
 * @return ESP_OK on success, or if @p handle is NULL or emulation was never initialized / already deinitialized (idempotent)
 *         ESP_ERR_INVALID_STATE if emulation handle exists but is not mapped (inconsistent state)
 *         ESP_ERR_INVALID_RESPONSE if cleanup operations fail
 */
esp_err_t nand_emul_deinit(spi_nand_flash_device_t *handle);

/**
 * @brief Read data from NAND flash
 *
 * @param handle spi_nand_flash_device_t handle for nand device
 * @param addr Source address in NAND flash
 * @param dst Destination buffer
 * @param size Number of bytes to read
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_STATE if there is no emulation handle or the backing mmap is not ready
 *         ESP_ERR_INVALID_SIZE if read would exceed flash size
 */
esp_err_t nand_emul_read(spi_nand_flash_device_t *handle, size_t addr, void *dst, size_t size);

/**
 * @brief Write data to NAND flash
 *
 * @param handle spi_nand_flash_device_t handle for nand device
 * @param addr Destination address in NAND flash
 * @param src Source data buffer
 * @param size Number of bytes to write
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_STATE if there is no emulation handle or the backing mmap is not ready
 *         ESP_ERR_INVALID_SIZE if write would exceed flash size
 */
esp_err_t nand_emul_write(spi_nand_flash_device_t *handle, size_t addr, const void *src, size_t size);

/**
 * @brief Erase a NAND block
 *
 * @param handle spi_nand_flash_device_t handle for nand device
 * @param offset Block Address offset to erase
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_STATE if there is no emulation handle or the backing mmap is not ready
 *         ESP_ERR_INVALID_SIZE if erase range would exceed emulated flash size
 */
esp_err_t nand_emul_erase_block(spi_nand_flash_device_t *handle, size_t offset);

#ifdef CONFIG_NAND_ENABLE_STATS
/**
 * @brief Get NAND operation statistics
 *
 * @param handle spi_nand_flash_device_t handle for nand device
 * @param[out] read_ops Number of read operations
 * @param[out] write_ops Number of write operations
 * @param[out] erase_ops Number of erase operations
 * @param[out] read_bytes Total bytes read
 * @param[out] write_bytes Total bytes written
 */
void nand_emul_get_stats(spi_nand_flash_device_t *handle, size_t *read_ops, size_t *write_ops, size_t *erase_ops,
                         size_t *read_bytes, size_t *write_bytes);

/**
 * @brief Clear NAND operation statistics
 * @param handle spi_nand_flash_device_t handle for nand device
 */
void nand_emul_clear_stats(spi_nand_flash_device_t *handle);
#endif /* CONFIG_NAND_ENABLE_STATS */

#ifdef __cplusplus
}
#endif
