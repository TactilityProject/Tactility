/*
 * SPDX-FileCopyrightText: 2022 mikkeldamsgaard project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPDX-FileContributor: 2015-2024 Espressif Systems (Shanghai) CO LTD
 */

#include <string.h>
#include "esp_check.h"
#include "spi_nand_flash.h"
#include "nand.h"
#include "nand_impl.h"
#include "nand_device_types.h"

#ifdef CONFIG_NAND_FLASH_ENABLE_BDL
#include "esp_blockdev.h"
#include "esp_nand_blockdev.h"
#endif

static const char *TAG = "nand_api";

esp_err_t spi_nand_flash_init_device(spi_nand_flash_config_t *config, spi_nand_flash_device_t **handle)
{
#ifdef CONFIG_NAND_FLASH_ENABLE_BDL
    ESP_LOGE(TAG, "spi_nand_flash_init_device() is not supported when BDL is enabled. "
             "Use spi_nand_flash_init_with_layers() instead");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!config->gc_factor) {
        config->gc_factor = 45;
    }

    esp_err_t ret = nand_init_device(config, handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nand_wl_attach_ops(*handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach wear-leveling operations");
    }

    if ((*handle)->ops->init == NULL) {
        ESP_LOGE(TAG, "Failed to initialize spi_nand_ops");
        ret = ESP_FAIL;
        return ret;
    }
    (*handle)->ops->init(*handle, NULL);

    return ESP_OK;
#endif // CONFIG_NAND_FLASH_ENABLE_BDL
}

esp_err_t spi_nand_erase_chip(spi_nand_flash_device_t *handle)
{
    ESP_LOGW(TAG, "Entire chip is being erased");
    esp_err_t ret = ESP_OK;

    if (handle->ops->erase_chip == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = handle->ops->erase_chip(handle);
    if (ret) {
        goto end;
    }
    handle->ops->deinit(handle);

end:
    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t spi_nand_flash_read_page(spi_nand_flash_device_t *handle, uint8_t *buffer, uint32_t page_id)
{
    esp_err_t ret = ESP_OK;

    if (handle->ops->read == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = handle->ops->read(handle, buffer, page_id);
    // After a successful read operation, check the ECC corrected bit status; if the read fails, return an error
    if (ret == ESP_OK && handle->chip.ecc_data.ecc_corrected_bits_status) {
        // This indicates a soft ECC error, we rewrite the page to recover if corrected bits are greater than refresh threshold
        if (nand_ecc_exceeds_data_refresh_threshold(handle)) {
            ret = handle->ops->write(handle, buffer, page_id);
        }
    }
    xSemaphoreGive(handle->mutex);

    return ret;
}

esp_err_t spi_nand_flash_read_sector(spi_nand_flash_device_t *handle, uint8_t *buffer, uint32_t sector_id)
{
    return spi_nand_flash_read_page(handle, buffer, sector_id);
}

esp_err_t spi_nand_flash_copy_page(spi_nand_flash_device_t *handle, uint32_t src_page, uint32_t dst_page)
{
    esp_err_t ret = ESP_OK;

    if (handle->ops->copy_sector == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = handle->ops->copy_sector(handle, src_page, dst_page);
    xSemaphoreGive(handle->mutex);

    return ret;
}

esp_err_t spi_nand_flash_copy_sector(spi_nand_flash_device_t *handle, uint32_t src_sec, uint32_t dst_sec)
{
    return spi_nand_flash_copy_page(handle, src_sec, dst_sec);
}

esp_err_t spi_nand_flash_write_page(spi_nand_flash_device_t *handle, const uint8_t *buffer, uint32_t page_id)
{
    esp_err_t ret = ESP_OK;

    if (handle->ops->write == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = handle->ops->write(handle, buffer, page_id);
    xSemaphoreGive(handle->mutex);

    return ret;
}

esp_err_t spi_nand_flash_write_sector(spi_nand_flash_device_t *handle, const uint8_t *buffer, uint32_t sector_id)
{
    return spi_nand_flash_write_page(handle, buffer, sector_id);
}

esp_err_t spi_nand_flash_trim(spi_nand_flash_device_t *handle, uint32_t page_id)
{
    esp_err_t ret = ESP_OK;

    if (handle->ops->trim == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = handle->ops->trim(handle, page_id);
    xSemaphoreGive(handle->mutex);

    return ret;
}

esp_err_t spi_nand_flash_sync(spi_nand_flash_device_t *handle)
{
    esp_err_t ret = ESP_OK;

    if (handle->ops->sync == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = handle->ops->sync(handle);
    xSemaphoreGive(handle->mutex);

    return ret;
}

esp_err_t spi_nand_flash_gc(spi_nand_flash_device_t *handle)
{
    esp_err_t ret = ESP_OK;

    if (handle->ops->gc == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = handle->ops->gc(handle);
    xSemaphoreGive(handle->mutex);

    return ret;
}

esp_err_t spi_nand_flash_get_page_count(spi_nand_flash_device_t *handle, uint32_t *number_of_pages)
{
    if (handle->ops->get_capacity == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return handle->ops->get_capacity(handle, number_of_pages);
}

esp_err_t spi_nand_flash_get_capacity(spi_nand_flash_device_t *handle, uint32_t *number_of_sectors)
{
    return spi_nand_flash_get_page_count(handle, number_of_sectors);
}

esp_err_t spi_nand_flash_get_page_size(spi_nand_flash_device_t *handle, uint32_t *page_size)
{
    *page_size = handle->chip.page_size;
    return ESP_OK;
}

esp_err_t spi_nand_flash_get_sector_size(spi_nand_flash_device_t *handle, uint32_t *sector_size)
{
    return spi_nand_flash_get_page_size(handle, sector_size);
}

esp_err_t spi_nand_flash_get_block_size(spi_nand_flash_device_t *handle, uint32_t *block_size)
{
    *block_size = handle->chip.block_size;
    return ESP_OK;
}

esp_err_t spi_nand_flash_get_block_num(spi_nand_flash_device_t *handle, uint32_t *num_blocks)
{
    *num_blocks = handle->chip.num_blocks;
    return ESP_OK;
}

esp_err_t spi_nand_flash_deinit_device(spi_nand_flash_device_t *handle)
{
    esp_err_t ret = ESP_OK;
#ifdef CONFIG_IDF_TARGET_LINUX
    ret = nand_emul_deinit(handle);
#endif
    nand_wl_detach_ops(handle);
    free(handle->work_buffer);
    free(handle->read_buffer);
#ifndef CONFIG_IDF_TARGET_LINUX
    free(handle->temp_buffer);
#endif
    if (handle->mutex) {
        vSemaphoreDelete(handle->mutex);
    }
    free(handle);
    return ret;
}

// NEW LAYERED ARCHITECTURE API IMPLEMENTATION
//---------------------------------------------------------------------------------------------------------------------------------------------

#ifdef CONFIG_NAND_FLASH_ENABLE_BDL
esp_err_t spi_nand_flash_init_with_layers(spi_nand_flash_config_t *config,
                                          esp_blockdev_handle_t *wl_bdl)
{
    ESP_RETURN_ON_FALSE(config && wl_bdl, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");

    // Set default GC factor if not specified
    if (!config->gc_factor) {
        config->gc_factor = 45;
    }

    // Initialize device and create Flash BDL
    esp_blockdev_handle_t flash_bdl;
    esp_err_t ret = nand_flash_get_blockdev(config, &flash_bdl);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to create Flash BDL");

    // Create WL BDL on top of Flash BDL (already a block device layer!)
    ret = spi_nand_flash_wl_get_blockdev(flash_bdl, wl_bdl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create WL BDL");
        (flash_bdl)->ops->release(flash_bdl);
        flash_bdl = NULL;
        return ret;
    }

    ESP_LOGD(TAG, "SPI NAND Flash initialized with layered block device architecture");
    return ESP_OK;
}
#endif // CONFIG_NAND_FLASH_ENABLE_BDL
