/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_nand_blockdev.h
 * @brief NAND Flash Block Device Interface
 *
 * This header provides the block device interface for SPI NAND Flash, including:
 * - Flash Block Device Layer (raw NAND flash access)
 * - Wear-Leveling Block Device Layer (logical sector access with wear leveling)
 * - NAND-specific ioctl commands and structures
 *
 * @note All block devices created by this interface use the standard esp_blockdev_t
 *       interface from ESP-IDF, making them compatible with filesystems and other
 *       block device consumers.
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "spi_nand_flash.h"
#include "nand_device_types.h"

#ifdef CONFIG_NAND_FLASH_ENABLE_BDL
#include "esp_blockdev.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// NAND-SPECIFIC IOCTL COMMANDS
//=============================================================================

#define ESP_BLOCKDEV_CMD_NAND_BASE                  (ESP_BLOCKDEV_CMD_SYSTEM_BASE + 10) /*!< Base for NAND-specific ioctl codes (10 above @ref ESP_BLOCKDEV_CMD_SYSTEM_BASE) */

/**
 * @defgroup esp_blockdev_nand_ioctl NAND flash block device ioctl commands
 * @brief NAND-specific ioctl commands for @ref esp_blockdev_ops_t::ioctl
 *
 * These extend the block device ioctl interface with bad-block handling, ECC status,
 * and other NAND-specific operations. Intended for raw NAND or FTL-backed block devices
 * that implement the corresponding cases in their ioctl handlers.
 *
 * @{
 */

/** @brief Check if a block is marked as bad
 *
 * @code{c}
 * esp_blockdev_cmd_arg_status_t cmd = { .num = block_num };
 * esp_err_t ret = bdl->ops->ioctl(bdl, ESP_BLOCKDEV_CMD_IS_BAD_BLOCK, &cmd);
 * bool is_bad = cmd.status;
 * @endcode
 */
#define ESP_BLOCKDEV_CMD_IS_BAD_BLOCK               (ESP_BLOCKDEV_CMD_NAND_BASE + 0)

/** @brief Mark a block as bad
 *
 * @code{c}
 * uint32_t block = test_block_num;
 * esp_err_t ret = bdl->ops->ioctl(bdl, ESP_BLOCKDEV_CMD_MARK_BAD_BLOCK, &block);
 * @endcode
 */
#define ESP_BLOCKDEV_CMD_MARK_BAD_BLOCK             (ESP_BLOCKDEV_CMD_NAND_BASE + 1)

/** @brief Check if a page is free
 *
 * @code{c}
 * esp_blockdev_cmd_arg_status_t cmd = { .num = page_num };
 * esp_err_t ret = bdl->ops->ioctl(bdl, ESP_BLOCKDEV_CMD_IS_FREE_PAGE, &cmd);
 * bool is_free = cmd.status;
 * @endcode
 */
#define ESP_BLOCKDEV_CMD_IS_FREE_PAGE               (ESP_BLOCKDEV_CMD_NAND_BASE + 2)

/** @brief Get ECC status for a specific page
 *
 * @code{c}
 * esp_blockdev_cmd_arg_ecc_status_t cmd = { .page_num = page_num };
 * esp_err_t ret = bdl->ops->ioctl(bdl, ESP_BLOCKDEV_CMD_GET_PAGE_ECC_STATUS, &cmd);
 * nand_ecc_status_t ecc_status = cmd.ecc_status;
 * @endcode
 */
#define ESP_BLOCKDEV_CMD_GET_PAGE_ECC_STATUS        (ESP_BLOCKDEV_CMD_NAND_BASE + 3)

/** @brief Get the number of bad blocks in the flash
 *
 * @code{c}
 * uint32_t bad_block_count;
 * esp_err_t ret = flash_bdl->ops->ioctl(flash_bdl, ESP_BLOCKDEV_CMD_GET_BAD_BLOCKS_COUNT, &bad_block_count);
 * @endcode
 */
#define ESP_BLOCKDEV_CMD_GET_BAD_BLOCKS_COUNT       (ESP_BLOCKDEV_CMD_NAND_BASE + 4)

/** @brief Get ECC error statistics
 *
 * Scans the device for ECC status; can be slow on large devices. Intended for
 * diagnostics and debugging (for example flash health and degradation checks), not for use
 * in the normal I/O path. Do not call from an ISR. On large parts a full scan
 * may run long enough to risk the task watchdog; call from a suitable task
 * context or adjust WDT settings if needed.
 *
 * @code{c}
 * esp_blockdev_cmd_arg_ecc_stats_t ecc_stats;
 * esp_err_t ret = flash_bdl->ops->ioctl(flash_bdl, ESP_BLOCKDEV_CMD_GET_ECC_STATS, &ecc_stats);
 * @endcode
 */
#define ESP_BLOCKDEV_CMD_GET_ECC_STATS              (ESP_BLOCKDEV_CMD_NAND_BASE + 5)

/** @brief Get complete NAND flash information (device ID and geometry)
 *
 * @code{c}
 * esp_blockdev_cmd_arg_nand_flash_info_t flash_info;
 * esp_err_t ret = flash_bdl->ops->ioctl(flash_bdl, ESP_BLOCKDEV_CMD_GET_NAND_FLASH_INFO, &flash_info);
 * printf("Manufacturer: 0x%02X, Device: 0x%04X\n",
 *        flash_info.device_info.manufacturer_id,
 *        flash_info.device_info.device_id);
 * @endcode
 */
#define ESP_BLOCKDEV_CMD_GET_NAND_FLASH_INFO        (ESP_BLOCKDEV_CMD_NAND_BASE + 6)

/** @brief Copy a page from source to destination (raw flash block device)
 *
 * Performs a hardware-level page copy where supported, preserving copy optimizations
 * available on the NAND device. Often used internally by wear-leveling layers.
 *
 * @code{c}
 * esp_blockdev_cmd_arg_copy_page_t copy_cmd = { .src_page = 10, .dst_page = 20 };
 * esp_err_t ret = flash_bdl->ops->ioctl(flash_bdl, ESP_BLOCKDEV_CMD_COPY_PAGE, &copy_cmd);
 * @endcode
 */
#define ESP_BLOCKDEV_CMD_COPY_PAGE                  (ESP_BLOCKDEV_CMD_NAND_BASE + 7)

/** @} */

//=============================================================================
// IOCTL COMMAND ARGUMENT STRUCTURES
//=============================================================================

/**
 * @brief Argument structure for block/page status commands
 *
 * Used with @ref ESP_BLOCKDEV_CMD_IS_BAD_BLOCK and @ref ESP_BLOCKDEV_CMD_IS_FREE_PAGE.
 */
typedef struct {
    uint32_t num;                                   /*!< IN: block or page number */
    bool status;                                    /*!< OUT: bad-block status (true if bad) or page-free status (true if free) */
} esp_blockdev_cmd_arg_status_t;

typedef esp_blockdev_cmd_arg_status_t esp_blockdev_cmd_arg_is_bad_block_t;
typedef esp_blockdev_cmd_arg_status_t esp_blockdev_cmd_arg_is_free_page_t;

/**
 * @brief Argument structure for ECC status query
 *
 * Used with @ref ESP_BLOCKDEV_CMD_GET_PAGE_ECC_STATUS.
 */
typedef struct {
    uint32_t page_num;                              /*!< IN: page number to check */
    nand_ecc_status_t ecc_status;                   /*!< OUT: ECC status (@ref nand_ecc_status_t) */
} esp_blockdev_cmd_arg_ecc_status_t;

/**
 * @brief ECC error statistics
 *
 * Used with @ref ESP_BLOCKDEV_CMD_GET_ECC_STATS.
 */
typedef struct {
    uint8_t ecc_threshold;                          /*!< Current ECC correction threshold */
    uint32_t ecc_total_err_count;                   /*!< Total number of ECC errors encountered */
    uint32_t ecc_uncorrected_err_count;             /*!< Number of uncorrectable ECC errors */
    uint32_t ecc_exceeding_threshold_err_count;     /*!< Number of errors exceeding threshold (data refresh recommended) */
} esp_blockdev_cmd_arg_ecc_stats_t;

/**
 * @brief Complete NAND flash device information
 *
 * Used with @ref ESP_BLOCKDEV_CMD_GET_NAND_FLASH_INFO.
 */
typedef struct {
    nand_device_info_t device_info;                 /*!< Device identification (manufacturer, device ID, chip name) */
    nand_flash_geometry_t geometry;                 /*!< Flash geometry (page size, block size, timing, and so on) */
} esp_blockdev_cmd_arg_nand_flash_info_t;

/**
 * @brief Argument structure for page copy command
 *
 * Used with @ref ESP_BLOCKDEV_CMD_COPY_PAGE.
 */
typedef struct {
    uint32_t src_page;                              /*!< IN: source page number */
    uint32_t dst_page;                              /*!< IN: destination page number */
} esp_blockdev_cmd_arg_copy_page_t;

//=============================================================================
// BLOCK DEVICE CREATION FUNCTIONS
//=============================================================================

/**
 * @brief Create Flash Block Device Layer (raw NAND flash access)
 *
 * This function initializes the NAND flash device and creates a block device
 * interface for direct physical access to the flash.
 *
 * @param[in]  config           Configuration for the SPI NAND flash device
 * @param[out] out_bdl_handle_ptr Pointer to store the Flash Block Device Layer handle
 *
 * @return
 *         - ESP_OK: Success
 *         - ESP_ERR_INVALID_ARG: Invalid configuration or NULL pointers
 *         - ESP_ERR_NO_MEM: Insufficient memory for device structures
 *         - ESP_ERR_NOT_FOUND: NAND device not detected on SPI bus
 *         - ESP_FAIL: Other initialization failure
 *
 * @note The returned block device handle must be released with bdl->ops->release(bdl)
 *       when no longer needed.
 *
 * @note This creates the FLASH layer. For filesystem use, you typically want
 *       the WEAR-LEVELING layer instead (see spi_nand_flash_wl_get_blockdev).
 *
 * @warning Raw flash access bypasses the Dhara FTL. For general read/write
 *          workloads and any standard filesystem use, prefer the wear-leveling
 *          BDL (`spi_nand_flash_wl_get_blockdev` / `spi_nand_flash_init_with_layers`)
 *
 * @warning @c erase on this handle performs a physical block erase only; it does not skip
 *          or validate bad blocks. If you must not erase blocks that are marked bad,
 *          query status with @ref ESP_BLOCKDEV_CMD_IS_BAD_BLOCK before erasing, or use the
 *          wear-leveling BDL, which manages bad blocks for you.
 */
esp_err_t nand_flash_get_blockdev(spi_nand_flash_config_t *config,
                                  esp_blockdev_handle_t *out_bdl_handle_ptr);

/**
 * @brief Create Wear-Leveling Block Device Layer (logical sector access)
 *
 * This function creates a wear-leveling block device on top of a Flash Block
 * Device Layer. The WL layer provides:
 * - Logical-to-physical sector mapping
 * - Automatic wear leveling (via Dhara library)
 * - Bad block abstraction (bad blocks invisible to user)
 * - Garbage collection
 * - Filesystem-ready interface
 *
 * @param[in]  nand_bdl          Flash Block Device Layer handle (from nand_flash_get_blockdev)
 * @param[out] out_bdl_handle_ptr Pointer to store the Wear-Leveling Block Device Layer handle
 *
 * @return
 *         - ESP_OK: Success
 *         - ESP_ERR_INVALID_ARG: Invalid flash BDL handle or NULL pointer
 *         - ESP_ERR_NO_MEM: Insufficient memory for wear-leveling structures
 *         - ESP_FAIL: Wear-leveling initialization failure
 *
 * @note The returned block device handle must be released with bdl->ops->release(bdl)
 *       when no longer needed.
 *
 * @note This is the recommended layer for filesystem use. It provides wear leveling
 *       and bad block management automatically.
 */
esp_err_t spi_nand_flash_wl_get_blockdev(esp_blockdev_handle_t nand_bdl,
                                         esp_blockdev_handle_t *out_bdl_handle_ptr);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_NAND_FLASH_ENABLE_BDL
