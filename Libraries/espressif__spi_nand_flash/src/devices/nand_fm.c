/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
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

static const char *TAG = "nand_fm";

esp_err_t spi_nand_fm_init(spi_nand_flash_device_t *dev)
{
    uint8_t device_id = 0;
    ESP_RETURN_ON_ERROR(spi_nand_read_device_id(dev, &device_id, sizeof(device_id)), TAG, "%s, Failed to get the device ID", __func__);
    dev->device_info.device_id = device_id;
    snprintf(dev->device_info.chip_name, sizeof(dev->device_info.chip_name),
             "fm-0x%02" PRIx8, device_id);
    ESP_LOGD(TAG, "%s: device_id: %x\n", __func__, device_id);

    dev->chip.has_quad_enable_bit = 1;
    dev->chip.quad_enable_bit_pos = 0;
    dev->chip.ecc_data.ecc_status_reg_len_in_bits = 3;
    dev->chip.erase_block_delay_us = 4000;
    dev->chip.program_page_delay_us = 400;
    dev->chip.read_page_delay_us = 105;
    switch (device_id) {
    case FM_DI_D5: //FM25S005BI3
        dev->chip.num_blocks = 512;
        dev->chip.log2_ppb = 6;        // 64 pages per block
        dev->chip.log2_page_size = 11; // 2048 bytes per page
        break;
    default:
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
