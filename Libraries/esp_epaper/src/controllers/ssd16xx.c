/**
 * @file ssd16xx.c
 * @brief Generic driver for SSD1680/SSD1681 based B/W e-paper displays
 *
 * Supports panels: 1.54", 2.13", 2.66", 2.7", 2.9", 3.7", 4.2"
 * All use same controller with different resolutions.
 */

#include "../epaper_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ssd16xx";

/*=============================================================================
 * Initialization
 *============================================================================*/

esp_err_t ssd16xx_init(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);
    const epd_panel_desc_t *panel = epd_get_panel(dev);
    uint8_t source_shift = panel->source_shift_bytes;
    uint16_t w = epd_get_width(dev);
    uint16_t h = epd_get_height(dev);

    ESP_LOGI(TAG, "Init SSD16xx panel: %dx%d", w, h);

    RESET(spi);
    vTaskDelay(pdMS_TO_TICKS(10));
    WAIT(dev);

    CMD(spi, 0x12);  // SWRESET
    WAIT(dev);

    CMD(spi, 0x01);  // Driver output control
    DATA(spi, (h - 1) & 0xFF);
    DATA(spi, (h - 1) >> 8);
    DATA(spi, 0x00);

    CMD(spi, 0x11);  // Data entry mode
    DATA(spi, 0x03);  // Y increment, X increment

    CMD(spi, 0x44);  // Set RAM X start/end (offset by source_shift for shifted panels)
    DATA(spi, source_shift);
    DATA(spi, source_shift + (w / 8) - 1);

    CMD(spi, 0x45);  // Set RAM Y start/end
    DATA(spi, 0x00);
    DATA(spi, 0x00);
    DATA(spi, (h - 1) & 0xFF);
    DATA(spi, (h - 1) >> 8);

    CMD(spi, 0x3C);  // Border waveform
    DATA(spi, 0x05);

    // Display update control. The second byte selects the OTP configuration: shifted
    // panels (source_shift_bytes != 0, e.g. GDEY029T71H) need 0x00 here, others 0x80.
    // GxEPD2: "OTP set for 168x384, but TFT shifted: S8..S175 connected to TFT sources".
    CMD(spi, 0x21);  // Display update control
    DATA(spi, 0x00);
    DATA(spi, panel->source_shift_bytes ? 0x00 : 0x80);

    CMD(spi, 0x18);  // Temperature sensor
    DATA(spi, 0x80);  // Internal sensor

    CMD(spi, 0x4E);  // Set RAM X counter (offset by source_shift)
    DATA(spi, source_shift);

    CMD(spi, 0x4F);  // Set RAM Y counter
    DATA(spi, 0x00);
    DATA(spi, 0x00);

    WAIT(dev);

    ESP_LOGI(TAG, "Panel initialized");
    return ESP_OK;
}

/*=============================================================================
 * Update Functions
 *============================================================================*/

static esp_err_t ssd16xx_update_full(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);
    const epd_panel_desc_t *panel = epd_get_panel(dev);

    // Display update control: bypass RED as 0. GxEPD2 writes this before every full
    // update. The fast variant additionally overrides the temperature register.
    CMD(spi, 0x21);
    DATA(spi, 0x40);
    DATA(spi, 0x00);

    if (panel->fast_full_update) {
        CMD(spi, 0x1A);  // Write temperature register
        DATA(spi, 0x6E);
        DATA(spi, 0x00);
        CMD(spi, 0x22);  // Load temperature value
        DATA(spi, 0x91);
        CMD(spi, 0x20);
        vTaskDelay(pdMS_TO_TICKS(2));

        CMD(spi, 0x22);  // Display update (reuse LUT)
        DATA(spi, 0xC7);
    } else {
        CMD(spi, 0x22);  // Display update (loads LUT, drives, powers down)
        DATA(spi, 0xF7);
    }

    CMD(spi, 0x20);  // Master activation
    WAIT(dev);

    return ESP_OK;
}

static esp_err_t ssd16xx_update_fast(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);

    // Fast init sequence
    RESET(spi);
    vTaskDelay(pdMS_TO_TICKS(10));

    CMD(spi, 0x12);  // SWRESET
    WAIT(dev);

    CMD(spi, 0x18);  // Temperature sensor
    DATA(spi, 0x80);

    CMD(spi, 0x22);  // Load temperature
    DATA(spi, 0xB1);
    CMD(spi, 0x20);
    WAIT(dev);

    CMD(spi, 0x1A);  // Write temperature register
    DATA(spi, 0x64);  // 100°C for faster refresh
    DATA(spi, 0x00);

    CMD(spi, 0x22);  // Load temperature
    DATA(spi, 0x91);
    CMD(spi, 0x20);
    WAIT(dev);

    // Fast update sequence
    CMD(spi, 0x22);
    DATA(spi, 0xC7);
    CMD(spi, 0x20);
    WAIT(dev);

    return ESP_OK;
}

static esp_err_t ssd16xx_update_partial(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);
    const epd_panel_desc_t *panel = epd_get_panel(dev);

    if (panel->ssd1685_partial) {
        // SSD1685 partial (GxEPD2 GDEY029T71H::_Update_Part): restore RED normal and
        // run the powered fast partial LUT (0x22=0xDC), which keeps the panel powered
        // so the next update starts without a full power-up cycle.
        CMD(spi, 0x21);  // Display update control
        DATA(spi, 0x00);
        DATA(spi, 0x00);
        CMD(spi, 0x22);  // Display update
        DATA(spi, 0xDC);
    } else {
        CMD(spi, 0x3C);  // Border waveform for partial
        DATA(spi, 0x80);
        CMD(spi, 0x22);  // Display update
        DATA(spi, 0xFF);  // Partial update sequence
    }

    CMD(spi, 0x20);  // Master activation
    WAIT(dev);

    return ESP_OK;
}

esp_err_t ssd16xx_update(epd_device_t *dev, epd_update_mode_t mode)
{
    switch (mode) {
        case EPD_UPDATE_FAST:
            return ssd16xx_update_fast(dev);
        case EPD_UPDATE_PARTIAL:
            return ssd16xx_update_partial(dev);
        case EPD_UPDATE_FULL:
        default:
            return ssd16xx_update_full(dev);
    }
}

/*=============================================================================
 * RAM Write Functions
 *============================================================================*/

esp_err_t ssd16xx_write_ram(epd_device_t *dev, const uint8_t *data, uint32_t len)
{
    epd_spi_t *spi = epd_get_spi(dev);
    const epd_panel_desc_t *panel = epd_get_panel(dev);
    bool partial_ready = epd_is_partial_ready(dev);

    // Reset RAM address to (source_shift, 0)
    CMD(spi, 0x4E);
    DATA(spi, panel->source_shift_bytes);
    CMD(spi, 0x4F);
    DATA(spi, 0x00);
    DATA(spi, 0x00);

    // Write to current RAM (0x24)
    CMD(spi, 0x24);
    epd_spi_write_data_bulk(spi, data, len);

    // Only write to base image RAM (0x26) when setting up for partial refresh
    // When partial_ready is true, we only update 0x24 so the display can
    // compare old (0x26) vs new (0x24) for partial update
    if (!partial_ready) {
        CMD(spi, 0x4E);
        DATA(spi, panel->source_shift_bytes);
        CMD(spi, 0x4F);
        DATA(spi, 0x00);
        DATA(spi, 0x00);

        CMD(spi, 0x26);
        epd_spi_write_data_bulk(spi, data, len);
    }

    return ESP_OK;
}

esp_err_t ssd16xx_write_ram_partial(epd_device_t *dev, uint16_t x, uint16_t y,
                                     uint16_t w, uint16_t h, const uint8_t *data)
{
    epd_spi_t *spi = epd_get_spi(dev);
    const epd_panel_desc_t *panel = epd_get_panel(dev);
    uint16_t panel_h = epd_get_height(dev);

    uint16_t x_start = x / 8 + panel->source_shift_bytes;
    uint16_t x_end = x_start + w / 8 - 1;
    uint16_t y_start = panel_h - 1 - y;
    uint16_t y_end = y_start - h + 1;

    // Set window
    CMD(spi, 0x44);
    DATA(spi, x_start);
    DATA(spi, x_end);

    CMD(spi, 0x45);
    DATA(spi, y_start & 0xFF);
    DATA(spi, (y_start >> 8) & 0xFF);
    DATA(spi, y_end & 0xFF);
    DATA(spi, (y_end >> 8) & 0xFF);

    // Set cursor
    CMD(spi, 0x4E);
    DATA(spi, x_start);

    CMD(spi, 0x4F);
    DATA(spi, y_start & 0xFF);
    DATA(spi, (y_start >> 8) & 0xFF);

    // Write RAM
    CMD(spi, 0x24);
    epd_spi_write_data_bulk(spi, data, (w / 8) * h);

    return ESP_OK;
}

/*=============================================================================
 * Power Management
 *============================================================================*/

esp_err_t ssd16xx_sleep(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);

    CMD(spi, 0x10);  // Deep sleep
    DATA(spi, 0x01);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Panel in deep sleep");
    return ESP_OK;
}

esp_err_t ssd16xx_wake(epd_device_t *dev)
{
    return ssd16xx_init(dev);
}
