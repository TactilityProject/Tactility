/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "esp_check.h"
#include "nand.h"
#include "spi_nand_oper.h"
#include "nand_flash_devices.h"

static const char *TAG = "nand_gigadevice";

esp_err_t spi_nand_gigadevice_init(spi_nand_flash_device_t *dev)
{
    uint8_t device_id = 0;
    ESP_RETURN_ON_ERROR(spi_nand_read_device_id(dev, &device_id, sizeof(device_id)), TAG, "%s, Failed to get the device ID", __func__);
    dev->device_info.device_id = device_id;
    snprintf(dev->device_info.chip_name, sizeof(dev->device_info.chip_name),
             "gigadevice-0x%02" PRIx8, device_id);
    ESP_LOGD(TAG, "%s: device_id: %x\n", __func__, device_id);

    dev->chip.has_quad_enable_bit = 1;
    dev->chip.quad_enable_bit_pos = 0;
    dev->chip.read_page_delay_us = 25;
    dev->chip.erase_block_delay_us = 3200;
    dev->chip.program_page_delay_us = 380;
    switch (device_id) {
    case GIGADEVICE_DI_51:
    case GIGADEVICE_DI_41:
    case GIGADEVICE_DI_31:
    case GIGADEVICE_DI_21:
    case GIGADEVICE_DI_81:
    case GIGADEVICE_DI_91:
        // GD5F1GQ5 / GD5F1GM7: single-plane, Internal Data Move has no parity restriction
        dev->chip.num_blocks = 1024;
        break;
    case GIGADEVICE_DI_32:
    case GIGADEVICE_DI_22:
        dev->chip.num_blocks = 2048;
        break;
    case GIGADEVICE_DI_52:
    case GIGADEVICE_DI_42:
        // GD5F2GQ5: single-plane; IDM requires same odd/even block parity
        dev->chip.num_blocks = 2048;
        dev->chip.flags = NAND_FLAG_IDM_SAME_PARITY_REQUIRED;
        break;
    case GIGADEVICE_DI_92:
    case GIGADEVICE_DI_82:
        // GD5F2GM7: single-plane; IDM requires same odd/even block parity
        dev->chip.num_blocks = 2048;
        dev->chip.flags = NAND_FLAG_IDM_SAME_PARITY_REQUIRED;
        break;
    case GIGADEVICE_DI_35:
    case GIGADEVICE_DI_25:
        dev->chip.num_blocks = 4096;
        break;
    case GIGADEVICE_DI_55:
    case GIGADEVICE_DI_45:
        // GD5F4GQ6: single-plane; IDM requires same odd/even block parity
        // (2Gb partition limit for IDM is deferred)
        dev->chip.num_blocks = 4096;
        dev->chip.flags = NAND_FLAG_IDM_SAME_PARITY_REQUIRED;
        break;
    case GIGADEVICE_DI_95:
    case GIGADEVICE_DI_85:
        // GD5F4GM8: single-plane; IDM requires same odd/even block parity
        // (2Gb partition limit for IDM is deferred)
        dev->chip.num_blocks = 4096;
        dev->chip.flags = NAND_FLAG_IDM_SAME_PARITY_REQUIRED;
        break;
    case GIGADEVICE_DI_94:
        // GD5F4GM7UExxG: single-plane; IDM requires same odd/even block parity
        dev->chip.log2_page_size = 12;  // 4096 bytes per page
        dev->chip.num_blocks = 2048;
        dev->chip.flags = NAND_FLAG_IDM_SAME_PARITY_REQUIRED;
        break;
    case GIGADEVICE_DI_99:
        // GD5F8GM8UExxG: single-plane; IDM requires same odd/even block parity
        dev->chip.log2_page_size = 12; // 4096 bytes per page
        dev->chip.num_blocks = 4096;
        dev->chip.flags = NAND_FLAG_IDM_SAME_PARITY_REQUIRED;
        break;
    default:
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
