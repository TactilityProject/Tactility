#include "mosaico_nand_extension.h"
/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "nand.h"
#include "esp_blockdev.h"
#include "esp_nand_blockdev.h"
#include "nand_device_types.h"

static const char *TAG = "nand_wl_blockdev";

/**************************************************************************************
 **************************************************************************************
 * Block Device Layer interface implementation
 **************************************************************************************
 */

static esp_err_t spi_nand_flash_wl_blockdev_read(esp_blockdev_handle_t handle, uint8_t *dst_buf, size_t dst_buf_size, uint64_t src_addr, size_t data_read_len)
{
    uint32_t page_size = (uint32_t)handle->geometry.read_size;

    if (page_size == 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (dst_buf == NULL || dst_buf_size < data_read_len) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((src_addr % page_size) != 0 || (data_read_len % page_size) != 0) {
        ESP_LOGE(TAG, "Source address 0x%.16" PRIx64 " or Read length 0x%08" PRIx32 " is not aligned to Page size %" PRIu32,
                 src_addr, (uint32_t)data_read_len, page_size);
        return ESP_ERR_INVALID_SIZE;
    }

    if (src_addr + data_read_len > handle->geometry.disk_size) {
        ESP_LOGE(TAG, "Read range exceeds device bounds");
        return ESP_ERR_INVALID_SIZE;
    }

    spi_nand_flash_device_t *dev_handle = (spi_nand_flash_device_t *)((esp_blockdev_handle_t)handle->ctx)->ctx;
    uint32_t start_page_id = (uint32_t)(src_addr >> dev_handle->chip.log2_page_size);
    uint32_t page_count = (uint32_t)(data_read_len >> dev_handle->chip.log2_page_size);

    esp_err_t ret = ESP_OK;
    for (uint32_t page_id = start_page_id; page_id < (start_page_id + page_count); page_id++) {
        ret = spi_nand_flash_read_page(dev_handle, dst_buf, page_id);
        if (ret) {
            ESP_LOGE(TAG, "%s, Failed to read the page, result=0x%08x", __func__, ret);
            return ret;
        }
        dst_buf += page_size;
    }
    ESP_LOGV(TAG, "read - src_addr=0x%.16" PRIx64 ", size=0x%08" PRIx32 ", result=0x%08x", src_addr, (uint32_t)data_read_len, ret);
    return ret;
}

static esp_err_t spi_nand_flash_wl_blockdev_write(esp_blockdev_handle_t handle, const uint8_t *src_buf, uint64_t dst_addr, size_t data_write_len)
{
    uint32_t page_size = (uint32_t)handle->geometry.write_size;

    if (handle->device_flags.read_only || page_size == 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (src_buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((dst_addr % page_size) != 0 || (data_write_len % page_size) != 0) {
        ESP_LOGE(TAG, "Write address 0x%" PRIx64 " not aligned to page size %" PRIu32, dst_addr, page_size);
        return ESP_ERR_INVALID_SIZE;
    }

    if (dst_addr + data_write_len > handle->geometry.disk_size) {
        ESP_LOGE(TAG, "Write range exceeds device bounds");
        return ESP_ERR_INVALID_SIZE;
    }

    spi_nand_flash_device_t *dev_handle = (spi_nand_flash_device_t *)((esp_blockdev_handle_t)handle->ctx)->ctx;
    uint32_t start_page_id = (uint32_t)(dst_addr >> dev_handle->chip.log2_page_size);
    uint32_t page_count = (uint32_t)(data_write_len >> dev_handle->chip.log2_page_size);
    esp_err_t ret = ESP_OK;
    for (uint32_t page_id = start_page_id; page_id < (start_page_id + page_count); page_id++) {
        ret = spi_nand_flash_write_page(dev_handle, src_buf, page_id);
        if (ret) {
            ESP_LOGE(TAG, "%s, Failed to write the page", __func__);
            return ret;
        }
        src_buf += page_size;
    }
    ESP_LOGV(TAG, "write - dst_addr=0x%.16" PRIx64 ", size=0x%08" PRIx32 ", result=0x%08x", dst_addr, (uint32_t)data_write_len, ret);
    return ret;
}

static esp_err_t spi_nand_flash_wl_blockdev_erase(esp_blockdev_handle_t handle, uint64_t start_addr, size_t erase_len)
{
    uint32_t page_size = (uint32_t)handle->geometry.write_size;
    uint32_t erase_size = (uint32_t)handle->geometry.erase_size;

    if (handle->device_flags.read_only || erase_size == 0 || page_size == 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if ((start_addr % erase_size) != 0 || (erase_len % erase_size) != 0 || (erase_size % page_size != 0)) {
        ESP_LOGE(TAG, "Start address 0x%" PRIx64 " or Erase length %zu not aligned to Erase size %" PRIu32,
                 start_addr, erase_len, erase_size);
        return ESP_ERR_INVALID_SIZE;
    }

    if (start_addr + erase_len > handle->geometry.disk_size) {
        ESP_LOGE(TAG, "Erase range exceeds device bounds");
        return ESP_ERR_INVALID_SIZE;
    }

    spi_nand_flash_device_t *dev_handle = (spi_nand_flash_device_t *)((esp_blockdev_handle_t)handle->ctx)->ctx;
    uint32_t page_count = (uint32_t)(erase_len >> dev_handle->chip.log2_page_size);
    uint32_t start_page_id = (uint32_t)(start_addr >> dev_handle->chip.log2_page_size);
    esp_err_t ret = ESP_OK;
    for (uint32_t page_id = start_page_id; page_id < (start_page_id + page_count); page_id++) {
        ret = spi_nand_flash_trim(dev_handle, page_id);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "%s, Failed to trim the page", __func__);
            return ret;
        }
    }
    /* Logical erase is TRIM: unmapped reads are FF. Dhara already
     * performs automatic GC in write/trim; copying one more page
     * here adds wear and latency for every LittleFS allocation. */
    ESP_LOGV(TAG, "erase - start_addr=0x%.16" PRIx64 ", size=0x%zx, result=0x%08x", start_addr, erase_len, ret);
    return ret;
}

static esp_err_t spi_nand_flash_wl_blockdev_sync(esp_blockdev_handle_t handle)
{
    spi_nand_flash_device_t *dev_handle = (spi_nand_flash_device_t *)((esp_blockdev_handle_t)handle->ctx)->ctx;
    return spi_nand_flash_sync(dev_handle);
}

static esp_err_t spi_nand_flash_wl_blockdev_ioctl(esp_blockdev_handle_t handle, const uint8_t cmd, void *args)
{
    if (args == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = ESP_OK;

    esp_blockdev_handle_t nand_bdl = (esp_blockdev_handle_t)handle->ctx;
    spi_nand_flash_device_t *dev_handle = (spi_nand_flash_device_t *)nand_bdl->ctx;

    switch (cmd) {

    case MOSAICO_NAND_DISCARD_ALL: {
        extern esp_err_t mosaico_nand_clear_map(spi_nand_flash_device_t *handle);
        if (*(const uint32_t *)args != MOSAICO_NAND_DISCARD_CONFIRM) return ESP_ERR_INVALID_ARG;
        return mosaico_nand_clear_map(dev_handle);
    }
    case ESP_BLOCKDEV_CMD_MARK_DELETED: {
        esp_blockdev_cmd_arg_erase_t *trim_arg = (esp_blockdev_cmd_arg_erase_t *)args;
        uint32_t page_size = (uint32_t)handle->geometry.write_size;
        if (page_size == 0) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        if ((trim_arg->start_addr % page_size) != 0 ||
                (trim_arg->erase_len % page_size != 0)) {
            ESP_LOGE(TAG, "Start address/length 0x%" PRIx64 "/%zu not aligned to page size %" PRIu32,
                     trim_arg->start_addr, trim_arg->erase_len, page_size);
            return ESP_ERR_INVALID_SIZE;
        }
        uint32_t page_count = (uint32_t)(trim_arg->erase_len >> dev_handle->chip.log2_page_size);
        uint32_t start_page_id = (uint32_t)(trim_arg->start_addr >> dev_handle->chip.log2_page_size);
        uint32_t total_pages = (uint32_t)(handle->geometry.disk_size / page_size);

        if ((start_page_id + page_count) > total_pages) {
            ESP_LOGE(TAG, "TRIM range exceeds device bounds: start_page_id=%"PRIu32" count=%"PRIu32" total=%"PRIu32"",
                     start_page_id, page_count, total_pages);
            return ESP_ERR_INVALID_ARG;
        }
        for (uint32_t page_id = start_page_id; page_id < (start_page_id + page_count); page_id++) {
            ret = spi_nand_flash_trim(dev_handle, page_id);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to trim page %"PRIu32"", page_id);
                return ret;
            }
        }
    }
    break;

    case ESP_BLOCKDEV_CMD_GET_NAND_FLASH_INFO:
    case ESP_BLOCKDEV_CMD_GET_BAD_BLOCKS_COUNT:
    case ESP_BLOCKDEV_CMD_GET_ECC_STATS: {
        ret = nand_bdl->ops->ioctl(nand_bdl, cmd, args);
    }
    break;

    default:
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ret;
}

static esp_err_t spi_nand_flash_wl_blockdev_release(esp_blockdev_handle_t handle)
{
    esp_blockdev_handle_t nand_handle = (esp_blockdev_handle_t)handle->ctx;
    spi_nand_flash_device_t *dev_handle = (spi_nand_flash_device_t *)nand_handle->ctx;

    nand_wl_detach_ops(dev_handle);
    esp_err_t ret = nand_handle->ops->release(nand_handle);
    free(handle);

    return ret;
}

static const esp_blockdev_ops_t spi_nand_flash_wl_blockdev_ops = {
    .read = spi_nand_flash_wl_blockdev_read,
    .write = spi_nand_flash_wl_blockdev_write,
    .erase = spi_nand_flash_wl_blockdev_erase,
    .ioctl = spi_nand_flash_wl_blockdev_ioctl,
    .sync = spi_nand_flash_wl_blockdev_sync,
    .release = spi_nand_flash_wl_blockdev_release,
};

esp_err_t spi_nand_flash_wl_get_blockdev(esp_blockdev_handle_t nand_bdl, esp_blockdev_handle_t *out_bdl_handle_ptr)
{
    // Validate input parameters
    ESP_RETURN_ON_FALSE(nand_bdl != NULL, ESP_ERR_INVALID_ARG, TAG, "nand_bdl cannot be NULL");
    ESP_RETURN_ON_FALSE(out_bdl_handle_ptr != NULL, ESP_ERR_INVALID_ARG, TAG, "out_bdl_handle_ptr cannot be NULL");

    spi_nand_flash_device_t *dev = (spi_nand_flash_device_t *)nand_bdl->ctx;
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_INVALID_ARG, TAG, "spi_nand_flash_device_t pointer cannot be NULL");

    // Validate that Flash BDL operations are available
    ESP_RETURN_ON_FALSE(nand_bdl->ops != NULL, ESP_ERR_INVALID_STATE, TAG, "Flash BDL ops cannot be NULL");
    ESP_RETURN_ON_FALSE(nand_bdl->ops->read != NULL, ESP_ERR_INVALID_STATE, TAG, "Flash BDL read operation is required");
    ESP_RETURN_ON_FALSE(nand_bdl->ops->write != NULL, ESP_ERR_INVALID_STATE, TAG, "Flash BDL write operation is required");
    ESP_RETURN_ON_FALSE(nand_bdl->ops->erase != NULL, ESP_ERR_INVALID_STATE, TAG, "Flash BDL erase operation is required");
    ESP_RETURN_ON_FALSE(nand_bdl->ops->ioctl != NULL, ESP_ERR_INVALID_STATE, TAG, "Flash BDL ioctl operation is required");
    ESP_RETURN_ON_FALSE(nand_bdl->ops->release != NULL, ESP_ERR_INVALID_STATE, TAG, "Flash BDL release operation is required");

    // Initialize ops with dhara operations
    esp_err_t ret = nand_wl_attach_ops(dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach wear-leveling operations");
        return ret;
    }

    // Initialize dhara lib
    if (dev->ops->init == NULL) {
        ESP_LOGE(TAG, "WL init operation is NULL");
        nand_wl_detach_ops(dev);
        return ESP_FAIL;
    }

    ret = dev->ops->init(dev, nand_bdl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize wear-leveling layer");
        nand_wl_detach_ops(dev);
        return ret;
    }

    esp_blockdev_t *blockdev = (esp_blockdev_t *) heap_caps_calloc(1, sizeof(esp_blockdev_t), MALLOC_CAP_DEFAULT);
    if (blockdev == NULL) {
        dev->ops->deinit(dev);
        nand_wl_detach_ops(dev);
        return ESP_ERR_NO_MEM;
    }
    blockdev->ctx = (void *)nand_bdl;

    blockdev->device_flags.read_only = 0;
    blockdev->device_flags.encrypted = 0;
    blockdev->device_flags.erase_before_write = 0;
    blockdev->device_flags.and_type_write = 0;
    blockdev->device_flags.default_val_after_erase = 1;
    blockdev->device_flags.reserved = 0;
    blockdev->ops = &spi_nand_flash_wl_blockdev_ops;

    // Set up geometry information (BDL exposes logical pages as "sectors" per esp_blockdev API)
    uint32_t num_pages;
    spi_nand_flash_get_page_count(dev, &num_pages);
    uint32_t page_size = dev->chip.page_size;

    blockdev->geometry.disk_size = (uint64_t)num_pages * page_size;
    blockdev->geometry.write_size = page_size;
    blockdev->geometry.read_size = page_size;
    blockdev->geometry.erase_size = page_size;
    blockdev->geometry.recommended_write_size = page_size;
    blockdev->geometry.recommended_read_size = page_size;
    blockdev->geometry.recommended_erase_size = page_size;

    *out_bdl_handle_ptr = blockdev;
    return ESP_OK;
}
