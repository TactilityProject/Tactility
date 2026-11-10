/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include "esp_check.h"
#include "esp_err.h"
#include "spi_nand_flash.h"
#include "nand.h"
#include "nand_linux_mmap_emul.h"

static const char *TAG = "nand_linux";

/* OOB marker layout at page data offset `page_size` (matches nand_impl.c HW path):
 * bytes 0-1: bad-block marker (0xFFFF = good / erased, 0x0000 = bad)
 * bytes 2-3: page-used marker (0xFFFF = free, 0x0000 = used after program) */
static const uint8_t s_oob_used_page_markers[4] = { 0xFF, 0xFF, 0x00, 0x00 };
static const uint8_t s_oob_mark_bad_markers[4] = { 0x00, 0x00, 0xFF, 0xFF };

/* Start of erase block `block` in the mmap file: ppb slots of (data + OOB) per page. */
static esp_err_t linux_mmap_block_file_offset(const spi_nand_flash_device_t *handle, uint32_t block, size_t *out_offset)
{
    ESP_RETURN_ON_FALSE(out_offset != NULL, ESP_ERR_INVALID_ARG, TAG, "out_offset is NULL");
    ESP_RETURN_ON_FALSE(handle->chip.num_blocks > 0, ESP_ERR_INVALID_STATE, TAG, "num_blocks is 0");
    ESP_RETURN_ON_FALSE(block < handle->chip.num_blocks, ESP_ERR_INVALID_ARG, TAG, "block index out of range");

    const uint64_t ppb = 1ull << handle->chip.log2_ppb;
    const uint64_t bytes_per_block = ppb * (uint64_t)handle->chip.emulated_page_size;
    const uint64_t off = (uint64_t)block * bytes_per_block;

    ESP_RETURN_ON_FALSE(off <= (uint64_t)SIZE_MAX, ESP_ERR_INVALID_SIZE, TAG, "mmap block offset overflow");
    *out_offset = (size_t)off;
    return ESP_OK;
}

static esp_err_t detect_chip(spi_nand_flash_device_t *dev)
{
    esp_err_t ret = ESP_OK;
    spi_nand_flash_config_t *config = &dev->config;

    ESP_GOTO_ON_ERROR(nand_emul_init(dev, config->emul_conf), fail, TAG, "");
    dev->chip.page_size = (1 << dev->chip.log2_page_size);

    dev->chip.emulated_page_oob = 64;  // The default page size is 2048, so the OOB size is 64.

    if (dev->chip.page_size == 512) {
        dev->chip.emulated_page_oob = 16;
    } else if (dev->chip.page_size == 2048) {
        dev->chip.emulated_page_oob = 64;
    } else if (dev->chip.page_size == 4096) {
        dev->chip.emulated_page_oob = 128;
    }
    dev->chip.emulated_page_size = dev->chip.page_size + dev->chip.emulated_page_oob;
    /* chip.block_size: user-visible data per erase block (matches nand_impl.c / BDL).
     * File layout interleaves OOB after each page; use file_bytes_per_block for num_blocks
     * and linux_mmap_block_file_offset() for mmap byte addresses. */
    dev->chip.block_size = (1u << dev->chip.log2_ppb) * dev->chip.page_size;
    const uint32_t file_bytes_per_block = (1u << dev->chip.log2_ppb) * dev->chip.emulated_page_size;

    if (dev->chip.block_size == 0 || file_bytes_per_block == 0) {
        ESP_LOGE(TAG, "Invalid block size (0)");
        ret = ESP_ERR_INVALID_SIZE;
        goto fail;
    }

    if (config->emul_conf->flash_file_size % dev->chip.block_size != 0) {
        ESP_LOGE(TAG, "flash_file_size (0x%" PRIx64 ") is not a multiple of chip.block_size (0x%" PRIx32 ")",
                 config->emul_conf->flash_file_size, dev->chip.block_size);
        ret = ESP_ERR_INVALID_SIZE;
        goto fail;
    }

    dev->chip.num_blocks = config->emul_conf->flash_file_size / file_bytes_per_block;
    dev->chip.erase_block_delay_us = 3000;
    dev->chip.program_page_delay_us = 630;
    dev->chip.read_page_delay_us = 60;

    /* Device info for GET_NAND_FLASH_INFO ioctl (host tests expect non-zero IDs and non-empty chip name) */
    dev->device_info.manufacturer_id = 0xEF;  /* Synthetic ID for Linux emulator */
    dev->device_info.device_id = 0xE100;
    strncpy(dev->device_info.chip_name, "Linux NAND mmap emul", sizeof(dev->device_info.chip_name) - 1);
    dev->device_info.chip_name[sizeof(dev->device_info.chip_name) - 1] = '\0';

fail:
    return ret;
}

esp_err_t nand_init_device(spi_nand_flash_config_t *config, spi_nand_flash_device_t **handle)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(config->emul_conf != NULL, ESP_ERR_INVALID_ARG, TAG, "Linux mmap emulation configuration pointer can not be NULL");

    *handle = heap_caps_calloc(1, sizeof(spi_nand_flash_device_t), MALLOC_CAP_DEFAULT);
    if (*handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(&(*handle)->config, config, sizeof(spi_nand_flash_config_t));

    (*handle)->chip.ecc_data.ecc_status_reg_len_in_bits = 2;
    (*handle)->chip.ecc_data.ecc_data_refresh_threshold = 4;
    (*handle)->chip.log2_ppb = 6;         // 64 pages per block is standard
    (*handle)->chip.log2_page_size = 11;  // 2048 bytes per page is fairly standard
    (*handle)->chip.num_planes = 1;
    (*handle)->chip.flags = 0;
    (*handle)->chip.page_size = 1 << (*handle)->chip.log2_page_size;
    (*handle)->chip.block_size = (1 << (*handle)->chip.log2_ppb) * (*handle)->chip.page_size;

    ESP_GOTO_ON_ERROR(detect_chip(*handle), fail, TAG, "Failed to detect nand chip");

    (*handle)->work_buffer = heap_caps_malloc((*handle)->chip.page_size, MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE((*handle)->work_buffer != NULL, ESP_ERR_NO_MEM, fail, TAG, "nomem");

    (*handle)->read_buffer = heap_caps_malloc((*handle)->chip.page_size, MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE((*handle)->read_buffer != NULL, ESP_ERR_NO_MEM, fail, TAG, "nomem");

    (*handle)->mutex = xSemaphoreCreateMutex();
    if (!(*handle)->mutex) {
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }
    return ret;

fail:
    free((*handle)->work_buffer);
    free((*handle)->read_buffer);
    if ((*handle)->mutex) {
        vSemaphoreDelete((*handle)->mutex);
    }
    free(*handle);
    *handle = NULL;
    return ret;
}

esp_err_t nand_is_bad(spi_nand_flash_device_t *handle, uint32_t block, bool *is_bad_status)
{
    uint8_t markers[4];
    size_t block_offset = 0;

    ESP_RETURN_ON_ERROR(linux_mmap_block_file_offset(handle, block, &block_offset), TAG, "nand_is_bad: mmap block offset failed");

    ESP_RETURN_ON_ERROR(nand_emul_read(handle, block_offset + handle->chip.page_size, markers, sizeof(markers)),
                        TAG, "Error in nand_is_bad");

    ESP_LOGD(TAG, "is_bad, block=%"PRIu32", file_off=%zu,indicator = %02x,%02x", block, block_offset, markers[0], markers[1]);
    *is_bad_status = (markers[0] != 0xFF || markers[1] != 0xFF);
    return ESP_OK;
}

esp_err_t nand_mark_bad(spi_nand_flash_device_t *handle, uint32_t block)
{
    size_t block_base = 0;

    uint64_t first_block_page = (uint64_t)block * (1ull << handle->chip.log2_ppb);
    ESP_LOGD(TAG, "mark_bad, block=%"PRIu32", first_page=%"PRIu64"", block, first_block_page);

    ESP_RETURN_ON_ERROR(linux_mmap_block_file_offset(handle, block, &block_base), TAG, "nand_mark_bad: mmap block offset failed");
    ESP_RETURN_ON_ERROR(nand_emul_erase_block(handle, block_base), TAG, "nand_mark_bad: erase failed");

    ESP_RETURN_ON_ERROR(nand_emul_write(handle, block_base + handle->chip.page_size,
                                        s_oob_mark_bad_markers, sizeof(s_oob_mark_bad_markers)), TAG, "nand_mark_bad: OOB marker write failed");

    return ESP_OK;
}

esp_err_t nand_erase_block(spi_nand_flash_device_t *handle, uint32_t block)
{
    ESP_LOGD(TAG, "erase_block, block=%"PRIu32",", block);
    esp_err_t ret = ESP_OK;
    size_t address = 0;

    ESP_RETURN_ON_ERROR(linux_mmap_block_file_offset(handle, block, &address), TAG, "nand_erase_block: mmap block offset failed");

    ESP_RETURN_ON_ERROR(nand_emul_erase_block(handle, address), TAG, "Error in nand_erase %x", ret);
    return ESP_OK;
}

static esp_err_t nand_erase_good_block(spi_nand_flash_device_t *handle, uint32_t block)
{
    ESP_LOGD(TAG, "erase_block, block=%"PRIu32",", block);
    esp_err_t ret = ESP_OK;
    bool is_bad = false;
    ret = nand_is_bad(handle, block, &is_bad);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error querying bad block status for block=%"PRIu32"", block);
        return ret;
    }
    if (is_bad) {
        ESP_LOGD(TAG, "skip erase of bad block=%"PRIu32"", block);
        return ESP_OK;
    }
    ret = nand_erase_block(handle, block);
    return ret;
}

esp_err_t nand_erase_chip(spi_nand_flash_device_t *handle)
{
    esp_err_t ret = ESP_OK;
    for (int i = 0; i < handle->chip.num_blocks; i++) {
        esp_err_t err = nand_erase_good_block(handle, (uint32_t)i);
        if (err == ESP_ERR_NOT_FINISHED) {
            ret = ESP_ERR_NOT_FINISHED;
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error in nand_erase_chip %d", err);
            return err;
        }
    }
    return ret;
}

esp_err_t nand_prog(spi_nand_flash_device_t *handle, uint32_t page, const uint8_t *data)
{
    ESP_LOGV(TAG, "prog, page=%"PRIu32",", page);
    esp_err_t ret = ESP_OK;
    uint32_t data_offset = page * handle->chip.emulated_page_size;

    ESP_RETURN_ON_ERROR(nand_emul_write(handle, data_offset, data, handle->chip.page_size), TAG, "Error in nand_prog %d", ret);
    ESP_RETURN_ON_ERROR(nand_emul_write(handle, data_offset + handle->chip.page_size,
                                        s_oob_used_page_markers, sizeof(s_oob_used_page_markers)), TAG, "Error in nand_prog %d", ret);

    return ret;
}

esp_err_t nand_is_free(spi_nand_flash_device_t *handle, uint32_t page, bool *is_free_status)
{
    esp_err_t ret = ESP_OK;
    uint8_t markers[4];

    ESP_RETURN_ON_ERROR(nand_emul_read(handle, page * handle->chip.emulated_page_size + handle->chip.page_size,
                                       markers, sizeof(markers)),
                        TAG, "Error in nand_is_free %d", ret);

    ESP_LOGD(TAG, "is free, page=%"PRIu32", used_marker=%02x%02x,", page, markers[2], markers[3]);
    *is_free_status = (markers[2] == 0xFF && markers[3] == 0xFF);
    return ret;
}

esp_err_t nand_read(spi_nand_flash_device_t *handle, uint32_t page, size_t offset, size_t length, uint8_t *data)
{
    ESP_LOGV(TAG, "read, page=%"PRIu32", offset=%ld, length=%ld", page, offset, length);
    assert(page < handle->chip.num_blocks * (1 << handle->chip.log2_ppb));
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_ERROR(nand_emul_read(handle, page * handle->chip.emulated_page_size + offset, data, length),
                        TAG, "Error in nand_read %d", ret);

    return ret;
}

esp_err_t nand_copy(spi_nand_flash_device_t *handle, uint32_t src, uint32_t dst)
{
    ESP_LOGD(TAG, "copy, src=%"PRIu32", dst=%"PRIu32"", src, dst);
    esp_err_t ret = ESP_OK;
    uint32_t dst_offset = dst * handle->chip.emulated_page_size;
    uint32_t src_offset = src * handle->chip.emulated_page_size;

    ESP_RETURN_ON_ERROR(nand_emul_read(handle, (size_t)src_offset, (void *)handle->read_buffer, handle->chip.page_size),
                        TAG, "Error in nand_copy %d", ret);
    ESP_RETURN_ON_ERROR(nand_emul_write(handle, (size_t)dst_offset, (void *)handle->read_buffer, handle->chip.page_size),
                        TAG, "Error in nand_copy %d", ret);
    ESP_RETURN_ON_ERROR(nand_emul_write(handle, (size_t)dst_offset + handle->chip.page_size,
                                        s_oob_used_page_markers, sizeof(s_oob_used_page_markers)), TAG, "Error in nand_copy %d", ret);

    return ret;
}

esp_err_t nand_get_ecc_status(spi_nand_flash_device_t *handle, uint32_t page)
{
    esp_err_t ret = ESP_OK;
    return ret;
}
