/**
 * @file acep_6color.c
 * @brief Driver for ACeP 6-Color E-Paper Displays (GDEP073E01, etc.)
 *
 * Colors: Black, White, Yellow, Red, Blue, Green
 * Format: 4-bit per pixel (2 pixels per byte)
 *
 * Note: This panel does NOT support partial or fast refresh.
 * Uses inverted busy signal (HIGH = ready, LOW = busy).
 */

#include "../epaper_common.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "acep6c";

/*=============================================================================
 * Initialization
 *============================================================================*/

esp_err_t acep6c_init(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);
    uint16_t w = epd_get_width(dev);
    uint16_t h = epd_get_height(dev);

    RESET(spi);
    vTaskDelay(pdMS_TO_TICKS(50));
    WAIT(dev);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Command sequence for 6-color e-paper
    CMD(spi, 0xAA);  // CMDH
    DATA(spi, 0x49);
    DATA(spi, 0x55);
    DATA(spi, 0x20);
    DATA(spi, 0x08);
    DATA(spi, 0x09);
    DATA(spi, 0x18);

    CMD(spi, 0x01);
    DATA(spi, 0x3F);

    CMD(spi, 0x00);
    DATA(spi, 0x5F);
    DATA(spi, 0x69);

    CMD(spi, 0x03);
    DATA(spi, 0x00);
    DATA(spi, 0x54);
    DATA(spi, 0x00);
    DATA(spi, 0x44);

    CMD(spi, 0x05);
    DATA(spi, 0x40);
    DATA(spi, 0x1F);
    DATA(spi, 0x1F);
    DATA(spi, 0x2C);

    CMD(spi, 0x06);
    DATA(spi, 0x6F);
    DATA(spi, 0x1F);
    DATA(spi, 0x17);
    DATA(spi, 0x49);

    CMD(spi, 0x08);
    DATA(spi, 0x6F);
    DATA(spi, 0x1F);
    DATA(spi, 0x1F);
    DATA(spi, 0x22);

    CMD(spi, 0x30);
    DATA(spi, 0x03);

    CMD(spi, 0x50);
    DATA(spi, 0x3F);

    CMD(spi, 0x60);
    DATA(spi, 0x02);
    DATA(spi, 0x00);

    // Resolution
    CMD(spi, 0x61);
    DATA(spi, (w >> 8) & 0xFF);
    DATA(spi, w & 0xFF);
    DATA(spi, (h >> 8) & 0xFF);
    DATA(spi, h & 0xFF);

    CMD(spi, 0x84);
    DATA(spi, 0x01);

    CMD(spi, 0xE3);
    DATA(spi, 0x2F);

    CMD(spi, 0x04);  // Power on
    WAIT(dev);

    ESP_LOGI(TAG, "Panel initialized (%dx%d 6-color)", w, h);
    return ESP_OK;
}

/*=============================================================================
 * Update (only full refresh supported)
 *============================================================================*/

esp_err_t acep6c_update(epd_device_t *dev, epd_update_mode_t mode)
{
    (void)mode;  // Only full refresh supported

    epd_spi_t *spi = epd_get_spi(dev);

    CMD(spi, 0x04);  // POWER_ON
    WAIT(dev);

    CMD(spi, 0x06);
    DATA(spi, 0x6F);
    DATA(spi, 0x1F);
    DATA(spi, 0x17);
    DATA(spi, 0x49);

    CMD(spi, 0x12);  // DISPLAY_REFRESH
    DATA(spi, 0x00);
    WAIT(dev);

    CMD(spi, 0x02);  // POWER_OFF
    DATA(spi, 0x00);
    WAIT(dev);

    return ESP_OK;
}

/*=============================================================================
 * RAM Write
 *============================================================================*/

esp_err_t acep6c_write_ram(epd_device_t *dev, const uint8_t *data, uint32_t len)
{
    epd_spi_t *spi = epd_get_spi(dev);

    CMD(spi, 0x10);  // Write RAM
    epd_spi_write_data_bulk(spi, data, len);

    return ESP_OK;
}

/*=============================================================================
 * Power Management
 *============================================================================*/

esp_err_t acep6c_sleep(epd_device_t *dev)
{
    epd_spi_t *spi = epd_get_spi(dev);

    CMD(spi, 0x07);  // DEEP_SLEEP
    DATA(spi, 0xA5);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Panel entering deep sleep");
    return ESP_OK;
}

esp_err_t acep6c_wake(epd_device_t *dev)
{
    return acep6c_init(dev);
}
