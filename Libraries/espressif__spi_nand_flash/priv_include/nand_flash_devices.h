/*
 * SPDX-FileCopyrightText: 2015-2025 Espressif Systems (Shanghai) CO LTD
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

//=============================================================================
// MANUFACTURER IDs
//=============================================================================

#define SPI_NAND_FLASH_GIGADEVICE_MI  0xC8
#define SPI_NAND_FLASH_ALLIANCE_MI    0x52
#define SPI_NAND_FLASH_WINBOND_MI     0xEF
#define SPI_NAND_FLASH_MICRON_MI      0x2C
#define SPI_NAND_FLASH_ZETTA_MI       0xBA
#define SPI_NAND_FLASH_XTX_MI         0x0B
#define SPI_NAND_FLASH_FM_MI          0xA1  // Fudan Microelectronics
#define SPI_NAND_FLASH_MACRONIX_MI    0xC2

//=============================================================================
// DEVICE IDs
//=============================================================================

// GigaDevice
#define GIGADEVICE_DI_51              0x51
#define GIGADEVICE_DI_41              0x41
#define GIGADEVICE_DI_31              0x31
#define GIGADEVICE_DI_21              0x21
#define GIGADEVICE_DI_52              0x52
#define GIGADEVICE_DI_42              0x42
#define GIGADEVICE_DI_32              0x32
#define GIGADEVICE_DI_22              0x22
#define GIGADEVICE_DI_55              0x55
#define GIGADEVICE_DI_45              0x45
#define GIGADEVICE_DI_35              0x35
#define GIGADEVICE_DI_25              0x25
#define GIGADEVICE_DI_95              0x95
#define GIGADEVICE_DI_85              0x85
#define GIGADEVICE_DI_92              0x92
#define GIGADEVICE_DI_82              0x82
#define GIGADEVICE_DI_91              0x91
#define GIGADEVICE_DI_81              0x81
#define GIGADEVICE_DI_94              0x94   // GD5F4GM7UExxG
#define GIGADEVICE_DI_99              0x99   // GD5F8GM8UExxG

// Alliance Memory
#define ALLIANCE_DI_25                0x25   // AS5F31G04SND-08LIN
#define ALLIANCE_DI_2E                0x2E   // AS5F32G04SND-08LIN
#define ALLIANCE_DI_8E                0x8E   // AS5F12G04SND-10LIN
#define ALLIANCE_DI_2F                0x2F   // AS5F34G04SND-08LIN
#define ALLIANCE_DI_8F                0x8F   // AS5F14G04SND-10LIN
#define ALLIANCE_DI_2D                0x2D   // AS5F38G04SND-08LIN
#define ALLIANCE_DI_8D                0x8D   // AS5F18G04SND-10LIN

// Winbond
#define WINBOND_DI_AA20               0xAA20
#define WINBOND_DI_BA20               0xBA20
#define WINBOND_DI_AA21               0xAA21
#define WINBOND_DI_BA21               0xBA21
#define WINBOND_DI_BC21               0xBC21
#define WINBOND_DI_AA22               0xAA22
#define WINBOND_DI_AA23               0xAA23

// Micron
#define MICRON_DI_34                  0x34
#define MICRON_DI_14                  0x14
#define MICRON_DI_15                  0x15
#define MICRON_DI_24                  0x24   // MT29F2G

// Zetta
#define ZETTA_DI_71                   0x71

// XTX
#define XTX_DI_37                     0x37

// Fudan Microelectronics
#define FM_DI_D5                      0xD5

// Macronix
#define MACRONIX_DI_26                0x26   //MX35LF2GE4AD (2Gb)
#define MACRONIX_DI_37                0x37   //MX35LF4GE4AD (4Gb)

//=============================================================================
// DEVICE INITIALIZATION FUNCTIONS
//=============================================================================

/**
 * @brief Initialize GigaDevice NAND flash
 */
esp_err_t spi_nand_gigadevice_init(spi_nand_flash_device_t *dev);

/**
 * @brief Initialize Alliance Memory NAND flash
 */
esp_err_t spi_nand_alliance_init(spi_nand_flash_device_t *dev);

/**
 * @brief Initialize Winbond NAND flash
 */
esp_err_t spi_nand_winbond_init(spi_nand_flash_device_t *dev);

/**
 * @brief Initialize Micron NAND flash
 */
esp_err_t spi_nand_micron_init(spi_nand_flash_device_t *dev);

/**
 * @brief Initialize Zetta NAND flash
 */
esp_err_t spi_nand_zetta_init(spi_nand_flash_device_t *dev);

/**
 * @brief Initialize XTX NAND flash
 */
esp_err_t spi_nand_xtx_init(spi_nand_flash_device_t *dev);

/**
 * @brief Initialize Fudan Microelectronics NAND flash
 */
esp_err_t spi_nand_fm_init(spi_nand_flash_device_t *dev);

/**
 * @brief Initialize Macronix NAND flash
 */
esp_err_t spi_nand_macronix_init(spi_nand_flash_device_t *dev);

#ifdef __cplusplus
}
#endif
