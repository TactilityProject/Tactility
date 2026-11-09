/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_check.h"
#include "esp_err.h"
#include "spi_nand_flash.h"
#include "nand.h"
#include "nand_impl.h"

esp_err_t nand_wrap_is_bad(spi_nand_flash_device_t *handle, uint32_t block, bool *is_bad_status)
{
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = nand_is_bad(handle, block, is_bad_status);
    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t nand_wrap_mark_bad(spi_nand_flash_device_t *handle, uint32_t block)
{
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = nand_mark_bad(handle, block);
    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t nand_wrap_erase_chip(spi_nand_flash_device_t *handle)
{
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = nand_erase_chip(handle);
    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t nand_wrap_erase_block(spi_nand_flash_device_t *handle, uint32_t block)
{
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = nand_erase_block(handle, block);
    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t nand_wrap_prog(spi_nand_flash_device_t *handle, uint32_t page, const uint8_t *data)
{
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = nand_prog(handle, page, data);
    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t nand_wrap_is_free(spi_nand_flash_device_t *handle, uint32_t page, bool *is_free_status)
{
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = nand_is_free(handle, page, is_free_status);
    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t nand_wrap_read(spi_nand_flash_device_t *handle, uint32_t page, size_t offset, size_t length, uint8_t *data)
{
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = nand_read(handle, page, offset, length, data);
    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t nand_wrap_copy(spi_nand_flash_device_t *handle, uint32_t src, uint32_t dst)
{
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = nand_copy(handle, src, dst);
    xSemaphoreGive(handle->mutex);
    return ret;
}

esp_err_t nand_wrap_get_ecc_status(spi_nand_flash_device_t *handle, uint32_t page)
{
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(handle->mutex, portMAX_DELAY);
    ret = nand_get_ecc_status(handle, page);
    xSemaphoreGive(handle->mutex);
    return ret;
}
