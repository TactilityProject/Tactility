/**
 * @file bwry_4color.c
 * @brief Driver for BWRY 4-Color E-Paper Displays (GDEY037F51, etc.)
 *
 * Colors: Black, White, Yellow, Red
 * Format: 2-bit per pixel (4 pixels per byte)
 *
 * Note: This panel does NOT support partial or fast refresh.
 * Uses inverted busy signal (HIGH = ready, LOW = busy).
 */

#include "../epaper_common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bwry4c";

/*=============================================================================
 * Initialization
 *============================================================================*/

esp_err_t bwry4c_init(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);
    uint16_t w = epd_get_width(dev);
    uint16_t h = epd_get_height(dev);

    vTaskDelay(pdMS_TO_TICKS(100));
    RESET(spi);
    vTaskDelay(pdMS_TO_TICKS(50));
    WAIT(dev);

    // PSR - Panel Setting
    CMD(spi, 0x00);
    DATA(spi, 0x0F);
    DATA(spi, 0x29);

    // PWR - Power Setting
    CMD(spi, 0x01);
    DATA(spi, 0x07);
    DATA(spi, 0x00);
    DATA(spi, 0x22);
    DATA(spi, 0x78);
    DATA(spi, 0x0A);
    DATA(spi, 0x22);

    // PFS - Power off sequence setting
    CMD(spi, 0x03);
    DATA(spi, 0x10);
    DATA(spi, 0x54);
    DATA(spi, 0x44);

    // BTST - Booster soft start
    CMD(spi, 0x06);
    DATA(spi, 0xC0);
    DATA(spi, 0xC0);
    DATA(spi, 0xC0);

    // PLL control
    CMD(spi, 0x30);
    DATA(spi, 0x08);

    // TSE - Temperature sensor enable
    CMD(spi, 0x41);
    DATA(spi, 0x00);

    // CDI - VCOM and data interval setting
    CMD(spi, 0x50);
    DATA(spi, 0x37);

    // TCON setting
    CMD(spi, 0x60);
    DATA(spi, 0x02);
    DATA(spi, 0x02);

    // Resolution setting
    CMD(spi, 0x61);
    DATA(spi, w >> 8);
    DATA(spi, w & 0xFF);
    DATA(spi, h >> 8);
    DATA(spi, h & 0xFF);

    // FLG - Get flag status
    CMD(spi, 0x65);
    DATA(spi, 0x00);
    DATA(spi, 0x00);
    DATA(spi, 0x00);
    DATA(spi, 0x00);

    // PWM settings
    CMD(spi, 0xE7);
    DATA(spi, 0x1C);

    CMD(spi, 0xE3);
    DATA(spi, 0x22);

    // Enter special register mode
    CMD(spi, 0xFF);
    DATA(spi, 0xA5);

    CMD(spi, 0xEF);
    DATA(spi, 0x01);
    DATA(spi, 0x1E);
    DATA(spi, 0x0A);
    DATA(spi, 0x1B);
    DATA(spi, 0x0B);
    DATA(spi, 0x17);

    CMD(spi, 0xC3);
    DATA(spi, 0xFD);

    CMD(spi, 0xDC);
    DATA(spi, 0x01);

    CMD(spi, 0xDD);
    DATA(spi, 0x08);

    CMD(spi, 0xDE);
    DATA(spi, 0x41);

    CMD(spi, 0xFD);
    DATA(spi, 0x01);

    CMD(spi, 0xE8);
    DATA(spi, 0x03);

    CMD(spi, 0xDA);
    DATA(spi, 0x07);

    CMD(spi, 0xC9);
    DATA(spi, 0x00);

    CMD(spi, 0xA8);
    DATA(spi, 0x0F);

    // Exit special register mode
    CMD(spi, 0xFF);
    DATA(spi, 0xE3);

    CMD(spi, 0xE9);
    DATA(spi, 0x01);

    // Power on
    CMD(spi, 0x04);
    WAIT(dev);

    // Additional PWM settings after power on
    CMD(spi, 0xFF);
    DATA(spi, 0xA5);

    CMD(spi, 0xEF);
    DATA(spi, 0x03);
    DATA(spi, 0x1E);
    DATA(spi, 0x0A);
    DATA(spi, 0x1B);
    DATA(spi, 0x0E);
    DATA(spi, 0x15);

    CMD(spi, 0xDC);
    DATA(spi, 0x01);

    CMD(spi, 0xDD);
    DATA(spi, 0x08);

    CMD(spi, 0xDE);
    DATA(spi, 0x41);

    CMD(spi, 0xFF);
    DATA(spi, 0xE3);

    ESP_LOGI(TAG, "Panel initialized (%dx%d 4-color BWRY)", w, h);
    return ESP_OK;
}

/*=============================================================================
 * Update (only full refresh supported)
 *============================================================================*/

esp_err_t bwry4c_update(epd_device_t *dev, epd_update_mode_t mode)
{
    (void)mode;  // Only full refresh supported

    epd_spi_t *spi = epd_get_spi(dev);

    CMD(spi, 0x12);  // Display Refresh
    DATA(spi, 0x00);
    WAIT(dev);

    return ESP_OK;
}

/*=============================================================================
 * RAM Write
 *============================================================================*/

esp_err_t bwry4c_write_ram(epd_device_t *dev, const uint8_t *data, uint32_t len)
{
    epd_spi_t *spi = epd_get_spi(dev);

    CMD(spi, 0x10);  // Write RAM
    epd_spi_write_data_bulk(spi, data, len);

    return ESP_OK;
}

/*=============================================================================
 * Power Management
 *============================================================================*/

esp_err_t bwry4c_sleep(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);

    CMD(spi, 0x02);  // Power off
    DATA(spi, 0x00);
    WAIT(dev);

    CMD(spi, 0x07);  // Deep sleep
    DATA(spi, 0xA5);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Panel entering deep sleep");
    return ESP_OK;
}

esp_err_t bwry4c_wake(epd_device_t *dev)
{
    return bwry4c_init(dev);
}
