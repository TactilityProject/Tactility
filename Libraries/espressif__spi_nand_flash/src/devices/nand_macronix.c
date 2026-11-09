/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_check.h"
#include "nand.h"
#include "spi_nand_oper.h"
#include "nand_flash_devices.h"

static const char *TAG = "nand_macronix";

esp_err_t spi_nand_macronix_init(spi_nand_flash_device_t *dev)
{
    uint8_t device_id = 0;
    ESP_RETURN_ON_ERROR(spi_nand_read_device_id(dev, &device_id, sizeof(device_id)), TAG, "%s, Failed to get the device ID", __func__);
    dev->device_info.device_id = device_id;
    snprintf(dev->device_info.chip_name, sizeof(dev->device_info.chip_name),
             "macronix-0x%02" PRIx8, device_id);
    ESP_LOGD(TAG, "%s: device_id: %x\n", __func__, device_id);

    dev->chip.has_quad_enable_bit = 1;
    dev->chip.quad_enable_bit_pos = 0;
    switch (device_id) {
    case MACRONIX_DI_26: // MX35LF2GE4AD (2Gb)
        dev->chip.num_blocks = 2048;
        dev->chip.log2_page_size = 11;  // 2048 bytes
        dev->chip.log2_ppb = 6;         // 64 pages per block
        dev->chip.read_page_delay_us = 10;
        dev->chip.erase_block_delay_us = 6000;
        dev->chip.program_page_delay_us = 400;
        break;
    case MACRONIX_DI_37: // MX35LF4GE4AD (4Gb)
        dev->chip.num_blocks = 2048;
        dev->chip.log2_page_size = 12;  // 4096 bytes
        dev->chip.log2_ppb = 6;         // 64 pages per block
        dev->chip.read_page_delay_us = 12;
        dev->chip.erase_block_delay_us = 6000;
        dev->chip.program_page_delay_us = 440;
        break;
    default:
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
