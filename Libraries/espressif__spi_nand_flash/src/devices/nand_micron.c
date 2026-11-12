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

static const char *TAG = "nand_micron";

esp_err_t spi_nand_micron_init(spi_nand_flash_device_t *dev)
{
    uint8_t device_id = 0;
    ESP_RETURN_ON_ERROR(spi_nand_read_device_id(dev, &device_id, sizeof(device_id)), TAG, "%s, Failed to get the device ID", __func__);
    dev->device_info.device_id = device_id;
    snprintf(dev->device_info.chip_name, sizeof(dev->device_info.chip_name),
             "micron-0x%02" PRIx8, device_id);
    ESP_LOGD(TAG, "%s: device_id: %x\n", __func__, device_id);

    dev->chip.has_quad_enable_bit = 0;
    dev->chip.quad_enable_bit_pos = 0;
    dev->chip.ecc_data.ecc_status_reg_len_in_bits = 3;
    dev->chip.erase_block_delay_us = 2000;
    switch (device_id) {
    case MICRON_DI_34:
        dev->chip.read_page_delay_us = 115;
        dev->chip.program_page_delay_us = 240;
        dev->chip.num_blocks = 2048;
        dev->chip.log2_ppb = 6;        // 64 pages per block
        dev->chip.log2_page_size = 12; // 4096 bytes per page
        break;
    case MICRON_DI_14:
    case MICRON_DI_15:
        dev->chip.read_page_delay_us = 46;
        dev->chip.program_page_delay_us = 220;
        dev->chip.num_blocks = 1024;
        dev->chip.log2_ppb = 6;          // 64 pages per block
        dev->chip.log2_page_size = 11;   // 2048 bytes per page
        break;
    case MICRON_DI_24:
        dev->chip.read_page_delay_us = 55;
        dev->chip.program_page_delay_us = 220;
        dev->chip.num_blocks = 2048;
        dev->chip.log2_ppb = 6;        // 64 pages per block
        dev->chip.log2_page_size = 11; // 2048 bytes per page
        dev->chip.flags = NAND_FLAG_HAS_PROG_PLANE_SELECT | NAND_FLAG_HAS_READ_PLANE_SELECT;
        dev->chip.num_planes = 2;
        break;
    default:
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
