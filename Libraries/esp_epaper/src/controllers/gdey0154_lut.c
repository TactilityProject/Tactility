/**
 * @file gdey0154_lut.c
 * @brief Driver for GDEY0154D67 1.54" B/W e-paper display with custom LUT
 *
 * This panel uses custom waveform lookup tables for optimized
 * full and partial refresh performance.
 */

#include "../epaper_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "gdey0154";

/*=============================================================================
 * Waveform LUTs
 *============================================================================*/

static const uint8_t LUT_FULL[159] = {
    0x80, 0x48, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x40, 0x48, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x80, 0x48, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x40, 0x48, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xA, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x8, 0x1, 0x0, 0x8, 0x1, 0x0, 0x2,
    0xA, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0,
    0x22, 0x17, 0x41, 0x0, 0x32, 0x20
};

static const uint8_t LUT_PARTIAL[159] = {
    0x0, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x80, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x40, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0xF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x1, 0x1, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0,
    0x02, 0x17, 0x41, 0xB0, 0x32, 0x28,
};

/*=============================================================================
 * LUT Loading
 *============================================================================*/

static void load_lut(epd_spi_t *spi, const uint8_t *lut)
{
    CMD(spi, 0x32);
    epd_spi_write_data_bulk(spi, lut, 153);
    epd_spi_wait_busy(spi, 0);

    CMD(spi, 0x3F);
    DATA(spi, lut[153]);

    CMD(spi, 0x03);
    DATA(spi, lut[154]);

    CMD(spi, 0x04);
    DATA(spi, lut[155]);
    DATA(spi, lut[156]);
    DATA(spi, lut[157]);

    CMD(spi, 0x2C);
    DATA(spi, lut[158]);
}

/*=============================================================================
 * Initialization
 *============================================================================*/

esp_err_t gdey0154_init(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);
    uint16_t w = epd_get_width(dev);
    uint16_t h = epd_get_height(dev);

    RESET(spi);
    vTaskDelay(pdMS_TO_TICKS(50));

    epd_spi_wait_busy(spi, 0);
    CMD(spi, 0x12);  // SWRESET
    epd_spi_wait_busy(spi, 0);

    CMD(spi, 0x01);  // Driver output control
    DATA(spi, 0xC7);
    DATA(spi, 0x00);
    DATA(spi, 0x01);

    CMD(spi, 0x11);  // Data entry mode
    DATA(spi, 0x01);

    // Set RAM window
    CMD(spi, 0x44);
    DATA(spi, 0);
    DATA(spi, (w - 1) / 8);

    CMD(spi, 0x45);
    DATA(spi, (h - 1) & 0xFF);
    DATA(spi, ((h - 1) >> 8) & 0xFF);
    DATA(spi, 0);
    DATA(spi, 0);

    CMD(spi, 0x3C);  // Border waveform
    DATA(spi, 0x01);

    CMD(spi, 0x18);  // Temperature sensor
    DATA(spi, 0x80);

    CMD(spi, 0x22);  // Load temperature
    DATA(spi, 0xB1);
    CMD(spi, 0x20);

    // Set cursor
    CMD(spi, 0x4E);
    DATA(spi, 0);

    CMD(spi, 0x4F);
    DATA(spi, (h - 1) & 0xFF);
    DATA(spi, ((h - 1) >> 8) & 0xFF);

    epd_spi_wait_busy(spi, 0);
    load_lut(spi, LUT_FULL);

    ESP_LOGD(TAG, "Init complete (%dx%d)", w, h);
    return ESP_OK;
}

/*=============================================================================
 * Update Functions
 *============================================================================*/

static esp_err_t gdey0154_update_full(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);

    load_lut(spi, LUT_FULL);

    CMD(spi, 0x22);
    DATA(spi, 0xC7);
    CMD(spi, 0x20);
    epd_spi_wait_busy(spi, 0);

    return ESP_OK;
}

static esp_err_t gdey0154_update_partial(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);

    RESET(spi);
    vTaskDelay(pdMS_TO_TICKS(50));
    epd_spi_wait_busy(spi, 0);

    load_lut(spi, LUT_PARTIAL);

    CMD(spi, 0x37);
    DATA(spi, 0x00);
    DATA(spi, 0x00);
    DATA(spi, 0x00);
    DATA(spi, 0x00);
    DATA(spi, 0x00);
    DATA(spi, 0x40);
    DATA(spi, 0x00);
    DATA(spi, 0x00);
    DATA(spi, 0x00);
    DATA(spi, 0x00);

    CMD(spi, 0x3C);  // Border waveform
    DATA(spi, 0x80);

    CMD(spi, 0x22);
    DATA(spi, 0xCF);
    CMD(spi, 0x20);
    epd_spi_wait_busy(spi, 0);

    return ESP_OK;
}

esp_err_t gdey0154_update(epd_device_t *dev, epd_update_mode_t mode)
{
    switch (mode) {
        case EPD_UPDATE_PARTIAL:
        case EPD_UPDATE_FAST:
            return gdey0154_update_partial(dev);
        case EPD_UPDATE_FULL:
        default:
            return gdey0154_update_full(dev);
    }
}

/*=============================================================================
 * RAM Write Functions
 *============================================================================*/

esp_err_t gdey0154_write_ram(epd_device_t *dev, const uint8_t *data, uint32_t len)
{
    epd_spi_t *spi = epd_get_spi(dev);
    bool partial_ready = epd_is_partial_ready(dev);

    // Write to current RAM (0x24)
    CMD(spi, 0x24);
    epd_spi_write_data_bulk(spi, data, len);

    // Only write to base image RAM (0x26) when setting up for partial refresh
    // When partial_ready is true, we only update 0x24 so the display can
    // compare old (0x26) vs new (0x24) for partial update
    if (!partial_ready) {
        CMD(spi, 0x26);
        epd_spi_write_data_bulk(spi, data, len);
    }

    return ESP_OK;
}

esp_err_t gdey0154_write_ram_partial(epd_device_t *dev, uint16_t x, uint16_t y,
                                      uint16_t w, uint16_t h, const uint8_t *data)
{
    epd_spi_t *spi = epd_get_spi(dev);
    uint16_t panel_h = epd_get_height(dev);

    uint16_t x_start = x / 8;
    uint16_t x_end = x_start + w / 8 - 1;
    uint16_t y_start = panel_h - 1 - y;
    uint16_t y_end = y_start - h + 1;

    CMD(spi, 0x44);
    DATA(spi, x_start);
    DATA(spi, x_end);

    CMD(spi, 0x45);
    DATA(spi, y_start & 0xFF);
    DATA(spi, (y_start >> 8) & 0xFF);
    DATA(spi, y_end & 0xFF);
    DATA(spi, (y_end >> 8) & 0xFF);

    CMD(spi, 0x4E);
    DATA(spi, x_start);

    CMD(spi, 0x4F);
    DATA(spi, y_start & 0xFF);
    DATA(spi, (y_start >> 8) & 0xFF);

    CMD(spi, 0x24);
    epd_spi_write_data_bulk(spi, data, (w / 8) * h);

    return ESP_OK;
}

/*=============================================================================
 * Power Management
 *============================================================================*/

esp_err_t gdey0154_sleep(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);

    CMD(spi, 0x10);
    DATA(spi, 0x01);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Panel in deep sleep");
    return ESP_OK;
}

esp_err_t gdey0154_wake(epd_device_t *dev)
{
    return gdey0154_init(dev);
}
