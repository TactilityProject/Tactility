/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <string.h>
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nand.h"
#include "nand_impl.h"
#include "nand_flash_devices.h"
#include "esp_nand_blockdev.h"
#include "nand_device_types.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "spi_nand_oper.h"
#endif

static const char *TAG = "nand_flash_blockdev";

/**************************************************************************************
 **************************************************************************************
 * Block Device Layer interface implementation
 **************************************************************************************
 */

static esp_err_t nand_flash_blockdev_read(esp_blockdev_handle_t handle, uint8_t *dst_buf, size_t dst_buf_size, uint64_t src_addr, size_t data_read_len)
{
    uint32_t page_size = (uint32_t)handle->geometry.read_size;

    if (page_size == 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (dst_buf == NULL || dst_buf_size < data_read_len) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_nand_flash_device_t *dev_handle = (spi_nand_flash_device_t *)handle->ctx;
    uint32_t start_page = (uint32_t)(src_addr >> dev_handle->chip.log2_page_size);
    uint32_t page_count = (uint32_t)(data_read_len >> dev_handle->chip.log2_page_size);
    size_t offset = src_addr % page_size;

    /* Single read: unaligned start or length not a multiple of page size */
    if (offset != 0 || (data_read_len % page_size) != 0) {
        if (offset + data_read_len > page_size) {
            ESP_LOGE(TAG, "Read crosses page boundary: offset=%zu + len=%zu > page_size=%" PRIu32,
                     offset, data_read_len, page_size);
            return ESP_ERR_INVALID_ARG;
        }
        if (src_addr + data_read_len > handle->geometry.disk_size) {
            ESP_LOGE(TAG, "Read range exceeds device bounds");
            return ESP_ERR_INVALID_SIZE;
        }
        return nand_read(dev_handle, start_page, offset, data_read_len, dst_buf);
    }

    /* Multi-page read: page-aligned address and length */
    if (src_addr + data_read_len > handle->geometry.disk_size) {
        ESP_LOGE(TAG, "Read range exceeds device bounds");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t res = ESP_OK;
    for (uint32_t page_id = start_page; page_id < start_page + page_count; page_id++) {
        res = nand_read(dev_handle, page_id, 0, page_size, dst_buf);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read page %" PRIu32, page_id);
            return res;
        }
        dst_buf += page_size;
    }
    ESP_LOGV(TAG, "read - src_addr=0x%.16" PRIx64 ", size=0x%08zx, result=0x%08x", src_addr, data_read_len, res);
    return res;
}

static esp_err_t nand_flash_blockdev_write(esp_blockdev_handle_t handle, const uint8_t *src_buf, uint64_t dst_addr, size_t data_write_len)
{
    uint32_t page_size = (uint32_t)handle->geometry.write_size;

    if (handle->device_flags.read_only || page_size == 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (src_buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((dst_addr % handle->geometry.write_size) != 0) {
        ESP_LOGE(TAG, "Write address 0x%" PRIx64 " not aligned to page size %" PRIu32, dst_addr,
                 (uint32_t)handle->geometry.write_size);
        return ESP_ERR_INVALID_SIZE;
    }

    if ((data_write_len % page_size) != 0) {
        ESP_LOGE(TAG, "Write length %zu not aligned to page size %" PRIu32, data_write_len, page_size);
        return ESP_ERR_INVALID_SIZE;
    }

    if (dst_addr + data_write_len > handle->geometry.disk_size) {
        ESP_LOGE(TAG, "Write range exceeds device bounds");
        return ESP_ERR_INVALID_SIZE;
    }

    spi_nand_flash_device_t *dev_handle = (spi_nand_flash_device_t *)handle->ctx;
    uint32_t start_page = (uint32_t)(dst_addr >> dev_handle->chip.log2_page_size);
    uint32_t page_count = (uint32_t)(data_write_len >> dev_handle->chip.log2_page_size);

    esp_err_t res = ESP_OK;
    for (uint32_t page_id = start_page; page_id < start_page + page_count; page_id++) {
        res = nand_prog(dev_handle, page_id, src_buf);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write page %" PRIu32, page_id);
            return res;
        }
        src_buf += page_size;
    }
    ESP_LOGV(TAG, "write - dst_addr=0x%.16" PRIx64 ", size=0x%08zx, result=0x%08x", dst_addr, data_write_len, res);
    return res;
}

static esp_err_t nand_flash_blockdev_erase(esp_blockdev_handle_t handle, uint64_t start_addr, size_t erase_len)
{
    uint32_t erase_size = (uint32_t)handle->geometry.erase_size;
    if (handle->device_flags.read_only || erase_size == 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if ((start_addr % erase_size) != 0) {
        ESP_LOGE(TAG, "Erase address 0x%" PRIx64 " not aligned to block size %" PRIu32, start_addr, erase_size);
        return ESP_ERR_INVALID_SIZE;
    }

    if (erase_len == 0 || (erase_len % erase_size) != 0) {
        ESP_LOGE(TAG, "Erase length %zu must be non-zero and a multiple of block size %" PRIu32, erase_len, erase_size);
        return ESP_ERR_INVALID_SIZE;
    }

    if (start_addr + erase_len > handle->geometry.disk_size) {
        ESP_LOGE(TAG, "Erase range exceeds device bounds");
        return ESP_ERR_INVALID_SIZE;
    }

    spi_nand_flash_device_t *dev_handle = (spi_nand_flash_device_t *)handle->ctx;
    uint8_t log2_block_size = dev_handle->chip.log2_page_size + dev_handle->chip.log2_ppb;
    uint32_t start_block = (uint32_t)(start_addr >> log2_block_size);
    uint32_t num_blocks = (uint32_t)(erase_len >> log2_block_size);
    esp_err_t ret = ESP_OK;
    for (uint32_t block = start_block; block < (start_block + num_blocks); block++) {
        ret = nand_erase_block(dev_handle, block);
        if (ret) {
            ESP_LOGE(TAG, "failed to erase block = %" PRIu32, block);
            break;
        }
    }
    ESP_LOGV(TAG, "erase - start_addr=0x%.16" PRIx64 ", size=0x%zx, result=0x%08x", start_addr, erase_len, ret);
    return ret;
}

static esp_err_t nand_flash_blockdev_sync_no_op(esp_blockdev_handle_t handle)
{
    return ESP_OK;
}

static esp_err_t nand_flash_blockdev_ioctl(esp_blockdev_handle_t handle, const uint8_t cmd, void *args)
{
    if (args == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_nand_flash_device_t *dev = (spi_nand_flash_device_t *)handle->ctx;

    switch (cmd) {
    case ESP_BLOCKDEV_CMD_IS_BAD_BLOCK: {
        esp_blockdev_cmd_arg_is_bad_block_t *bad_block_status = (esp_blockdev_cmd_arg_is_bad_block_t *) args;
        esp_err_t ret = nand_is_bad(dev, bad_block_status->num, &bad_block_status->status);
        return ret;
    }

    case ESP_BLOCKDEV_CMD_MARK_BAD_BLOCK: {
        uint32_t *block = (uint32_t *) args;
        esp_err_t ret = nand_mark_bad(dev, *block);
        return ret;
    }

    case ESP_BLOCKDEV_CMD_IS_FREE_PAGE: {
        esp_blockdev_cmd_arg_is_free_page_t *page_free_status = (esp_blockdev_cmd_arg_is_free_page_t *) args;
        esp_err_t ret = nand_is_free(dev, page_free_status->num, &page_free_status->status);
        return ret;
    }

    case ESP_BLOCKDEV_CMD_GET_PAGE_ECC_STATUS: {
        esp_blockdev_cmd_arg_ecc_status_t *page_ecc_status = (esp_blockdev_cmd_arg_ecc_status_t *) args;
        esp_err_t ret = nand_get_ecc_status(dev, page_ecc_status->page_num);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get ECC status for page=%"PRIu32"", page_ecc_status->page_num);
            return ret;
        }
        page_ecc_status->ecc_status = dev->chip.ecc_data.ecc_corrected_bits_status;
        return ret;
    }

    case ESP_BLOCKDEV_CMD_GET_NAND_FLASH_INFO: {
        esp_blockdev_cmd_arg_nand_flash_info_t *flash_info = (esp_blockdev_cmd_arg_nand_flash_info_t *)args;
        flash_info->device_info.manufacturer_id = dev->device_info.manufacturer_id;
        flash_info->device_info.device_id = dev->device_info.device_id;
        memcpy(flash_info->device_info.chip_name, dev->device_info.chip_name, sizeof(dev->device_info.chip_name));
        memcpy(&flash_info->geometry, &dev->chip, sizeof(nand_flash_geometry_t));
        return ESP_OK;
    }

    case ESP_BLOCKDEV_CMD_GET_BAD_BLOCKS_COUNT: {
        uint32_t *bad_block_count = (uint32_t *)args;
        uint32_t num_blocks = dev->chip.num_blocks;
        uint32_t bad_blocks = 0;
        esp_err_t ret = ESP_OK;
        for (uint32_t blk = 0; blk < num_blocks; blk++) {
            bool is_bad = false;
            ret = nand_is_bad(dev, blk, &is_bad);
            if (ret == ESP_OK && is_bad) {
                bad_blocks++;
                ESP_LOGD(TAG, "bad block num=%"PRIu32"", blk);
            } else if (ret) {
                ESP_LOGE(TAG, "Failed to get bad block status for blk=%"PRIu32"", blk);
                return ret;
            }
        }
        *bad_block_count = bad_blocks;
        return ret;
    }

    case ESP_BLOCKDEV_CMD_COPY_PAGE: {
        esp_blockdev_cmd_arg_copy_page_t *copy_cmd = (esp_blockdev_cmd_arg_copy_page_t *)args;
        esp_err_t ret = nand_copy(dev, copy_cmd->src_page, copy_cmd->dst_page);
        return ret;
    }

    /* Full device scan for diagnostics; can be slow on large devices. Intended for debug/health checks only. */
    case ESP_BLOCKDEV_CMD_GET_ECC_STATS: {
        esp_blockdev_cmd_arg_ecc_stats_t *ecc_stats = (esp_blockdev_cmd_arg_ecc_stats_t *)args;

        if (handle->geometry.write_size == 0) {
            return ESP_ERR_NOT_SUPPORTED;
        }

        uint32_t num_pages = (uint32_t)(handle->geometry.disk_size / handle->geometry.write_size);
        bool is_free = true;
        uint32_t ecc_err_total_count = 0;
        uint32_t ecc_err_exceeding_threshold_count = 0;
        uint32_t ecc_err_not_corrected_count = 0;
        esp_err_t ret = ESP_OK;
        for (uint32_t page = 0; page < num_pages; page++) {
            ret = nand_is_free(dev, page, &is_free);
            if (ret == ESP_OK && !is_free) {
                ret = nand_get_ecc_status(dev, page);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to read ecc error for page=%" PRIu32, page);
                    return ret;
                }
                if (dev->chip.ecc_data.ecc_corrected_bits_status) {
                    ecc_err_total_count++;
                    if (dev->chip.ecc_data.ecc_corrected_bits_status == NAND_ECC_NOT_CORRECTED) {
                        ecc_err_not_corrected_count++;
                        ESP_LOGD(TAG, "ecc error not corrected for page=%" PRIu32, page);
                    } else if (nand_ecc_exceeds_data_refresh_threshold(dev)) {
                        ecc_err_exceeding_threshold_count++;
                    }
                }
            }
        }
        ecc_stats->ecc_threshold = dev->chip.ecc_data.ecc_data_refresh_threshold;
        ecc_stats->ecc_total_err_count = ecc_err_total_count;
        ecc_stats->ecc_uncorrected_err_count = ecc_err_not_corrected_count;
        ecc_stats->ecc_exceeding_threshold_err_count = ecc_err_exceeding_threshold_count;
        return ret;
    }

    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

static esp_err_t nand_flash_blockdev_release(esp_blockdev_handle_t handle)
{
    esp_err_t res = ESP_OK;
    spi_nand_flash_device_t *dev_handle = (spi_nand_flash_device_t *)handle->ctx;
#ifdef CONFIG_IDF_TARGET_LINUX
    res = nand_emul_deinit(dev_handle);
#endif
    free(dev_handle->work_buffer);
    free(dev_handle->read_buffer);
    free(dev_handle->temp_buffer);
    if (dev_handle->mutex) {
        vSemaphoreDelete(dev_handle->mutex);
    }
    free(dev_handle);
    free(handle);
    return res;
}

static const esp_blockdev_ops_t nand_flash_blockdev_ops = {
    .read = nand_flash_blockdev_read,
    .write = nand_flash_blockdev_write,
    .erase = nand_flash_blockdev_erase,
    .ioctl = nand_flash_blockdev_ioctl,
    .sync = nand_flash_blockdev_sync_no_op,
    .release = nand_flash_blockdev_release,
};

esp_err_t nand_flash_get_blockdev(spi_nand_flash_config_t *config, esp_blockdev_handle_t *out_bdl_handle_ptr)
{
    if (config == NULL || out_bdl_handle_ptr == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_nand_flash_device_t *handle = NULL;
    esp_err_t ret = nand_init_device(config, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    esp_blockdev_t *blockdev = (esp_blockdev_t *) heap_caps_calloc(1, sizeof(esp_blockdev_t), MALLOC_CAP_DEFAULT);
    if (blockdev == NULL) {
        free(handle->work_buffer);
        free(handle->read_buffer);
        free(handle->temp_buffer);
        if (handle->mutex) {
            vSemaphoreDelete(handle->mutex);
        }
        free(handle);
        return ESP_ERR_NO_MEM;
    }
    blockdev->ctx = (void *)handle;
    *out_bdl_handle_ptr = blockdev;

    blockdev->ops = &nand_flash_blockdev_ops;

    blockdev->device_flags.read_only = 0;
    blockdev->device_flags.encrypted = 0;
    blockdev->device_flags.erase_before_write = 1;
    blockdev->device_flags.and_type_write = 1;
    blockdev->device_flags.default_val_after_erase = 1;
    blockdev->device_flags.reserved = 0;

    // Set up geometry information
    uint32_t page_size = handle->chip.page_size;
    uint32_t block_size = handle->chip.block_size;
    uint32_t num_blocks = handle->chip.num_blocks;

    blockdev->geometry.disk_size = num_blocks * block_size;
    blockdev->geometry.write_size = page_size;
    blockdev->geometry.read_size = page_size;
    blockdev->geometry.erase_size = block_size;
    blockdev->geometry.recommended_write_size = page_size;
    blockdev->geometry.recommended_read_size = page_size;
    blockdev->geometry.recommended_erase_size = block_size;

    return ESP_OK;
}
