/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief NAND Flash ECC status enumeration */
typedef enum {
    NAND_ECC_OK = 0,                     /*!< No ECC errors detected */
    NAND_ECC_1_TO_3_BITS_CORRECTED = 1, /*!< 1-3 bits corrected */
    NAND_ECC_BITS_CORRECTED = NAND_ECC_1_TO_3_BITS_CORRECTED,
    NAND_ECC_NOT_CORRECTED = 2,          /*!< ECC errors not correctable */
    NAND_ECC_4_TO_6_BITS_CORRECTED = 3, /*!< 4-6 bits corrected */
    NAND_ECC_MAX_BITS_CORRECTED = NAND_ECC_4_TO_6_BITS_CORRECTED,
    NAND_ECC_7_8_BITS_CORRECTED = 5,    /*!< 7-8 bits corrected */
    NAND_ECC_MAX
} nand_ecc_status_t;

/** @brief NAND Flash ECC configuration and status */
typedef struct {
    uint8_t ecc_status_reg_len_in_bits;     /*!< Length of ECC status register in bits */
    uint8_t ecc_data_refresh_threshold;     /*!< ECC error threshold for data refresh */
    nand_ecc_status_t ecc_corrected_bits_status; /*!< Current ECC correction status */
} nand_ecc_data_t;

/** @brief NAND Flash chip geometry and characteristics */
typedef struct {
    uint8_t log2_page_size;                 /*!< Page size as power of 2 (e.g., 11 for 2048 bytes) */
    uint8_t log2_ppb;                       /*!< Pages per block as power of 2 (e.g., 6 for 64 pages) */
    uint32_t block_size;                    /*!< Block size in bytes */
    uint32_t page_size;                     /*!< Page size in bytes */
    uint32_t num_blocks;                    /*!< Total number of blocks */
    uint32_t read_page_delay_us;            /*!< Read page delay in microseconds */
    uint32_t erase_block_delay_us;          /*!< Erase block delay in microseconds */
    uint32_t program_page_delay_us;         /*!< Program page delay in microseconds */
    uint32_t num_planes;                    /*!< Number of planes in the flash */
    uint32_t flags;                         /*!< Chip-specific flags */
    nand_ecc_data_t ecc_data;              /*!< ECC configuration and status */
    uint8_t has_quad_enable_bit;           /*!< 1 if chip supports QIO/QOUT mode */
    uint8_t quad_enable_bit_pos;           /*!< Position of quad enable bit */
#ifdef CONFIG_IDF_TARGET_LINUX
    uint32_t emulated_page_size;            /*!< Emulated page size for Linux */
    uint32_t emulated_page_oob;             /*!< Emulated OOB size for Linux */
#endif
} nand_flash_geometry_t;

/** @brief NAND Flash device identification information */
typedef struct {
    uint8_t manufacturer_id;                /*!< Manufacturer ID */
    uint16_t device_id;                     /*!< Device ID */
    char chip_name[32];                     /*!< Chip name string */
} nand_device_info_t;

#ifdef __cplusplus
}
#endif
