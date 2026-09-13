#include "epaper_spi.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd_spi";

#define SPI_MAX_CHUNK_SIZE  4096  // 4KB per transfer (safe for all ESP32 variants)

esp_err_t epd_spi_init(epd_spi_t *spi, const epd_pin_config_t *pins, const epd_spi_config_t *spi_cfg)
{
    // Configure GPIO pins
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    // DC, RST, CS as output
    io_conf.pin_bit_mask = (1ULL << pins->dc) | (1ULL << pins->rst) | (1ULL << pins->cs);
    gpio_config(&io_conf);
    
    // BUSY as input
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << pins->busy);
    gpio_config(&io_conf);
    
    // Store pins
    spi->pin_dc = pins->dc;
    spi->pin_rst = pins->rst;
    spi->pin_busy = pins->busy;
    spi->pin_cs = pins->cs;
    
    // Set initial state
    gpio_set_level(pins->cs, 1);
    gpio_set_level(pins->dc, 1);
    gpio_set_level(pins->rst, 1);
    
    // Configure SPI bus (large buffer for big displays like 800x480)
    spi_bus_config_t buscfg = {
        .mosi_io_num = pins->mosi,
        .miso_io_num = -1,  // Not used
        .sclk_io_num = pins->sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SPI_MAX_CHUNK_SIZE,  // Match chunk size
    };
    
    esp_err_t ret = spi_bus_initialize(spi_cfg->host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Add SPI device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = spi_cfg->speed_hz,
        .mode = 0,
        .spics_io_num = -1,  // Manual CS control
        .queue_size = 1,
    };
    
    ret = spi_bus_add_device(spi_cfg->host, &devcfg, &spi->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "SPI initialized");
    return ESP_OK;
}

esp_err_t epd_spi_deinit(epd_spi_t *spi)
{
    if (spi->spi) {
        spi_bus_remove_device(spi->spi);
        spi->spi = NULL;
    }
    return ESP_OK;
}

void epd_spi_write_cmd(epd_spi_t *spi, uint8_t cmd)
{
    gpio_set_level(spi->pin_cs, 0);
    gpio_set_level(spi->pin_dc, 0);  // Command mode
    
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(spi->spi, &t);
    
    gpio_set_level(spi->pin_cs, 1);
}

void epd_spi_write_data(epd_spi_t *spi, uint8_t data)
{
    gpio_set_level(spi->pin_cs, 0);
    gpio_set_level(spi->pin_dc, 1);  // Data mode
    
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &data,
    };
    spi_device_polling_transmit(spi->spi, &t);
    
    gpio_set_level(spi->pin_cs, 1);
}

void epd_spi_write_data_bulk(epd_spi_t *spi, const uint8_t *data, uint32_t len)
{
    gpio_set_level(spi->pin_cs, 0);
    gpio_set_level(spi->pin_dc, 1);  // Data mode
    
    // Send in chunks to avoid SPI transfer limit
    uint32_t offset = 0;
    while (offset < len) {
        uint32_t chunk_size = (len - offset > SPI_MAX_CHUNK_SIZE) ? SPI_MAX_CHUNK_SIZE : (len - offset);
        spi_transaction_t t = {
            .length = chunk_size * 8,
            .tx_buffer = data + offset,
        };
        spi_device_polling_transmit(spi->spi, &t);
        offset += chunk_size;
    }
    
    gpio_set_level(spi->pin_cs, 1);
}

void epd_spi_reset(epd_spi_t *spi)
{
    gpio_set_level(spi->pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(spi->pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

int epd_spi_is_busy(epd_spi_t *spi)
{
    return gpio_get_level(spi->pin_busy);
}

void epd_spi_wait_busy(epd_spi_t *spi, uint32_t timeout_ms)
{
    uint32_t start = xTaskGetTickCount();
    while (gpio_get_level(spi->pin_busy)) {
        if (timeout_ms > 0) {
            uint32_t elapsed = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
            if (elapsed >= timeout_ms) {
                ESP_LOGW(TAG, "Wait busy timeout");
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));  // 5ms like reference
    }
}
