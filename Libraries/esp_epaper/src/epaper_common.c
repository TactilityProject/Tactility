#include "epaper_common.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "epd_common";

void epd_wait_busy_polarity(epd_spi_t *spi, uint32_t caps, uint32_t timeout_ms)
{
    bool inverted = (caps & EPD_CAP_BUSY_INV);
    uint32_t start = xTaskGetTickCount();

    // For inverted: wait while LOW (busy), ready when HIGH
    // For normal: wait while HIGH (busy), ready when LOW
    int busy_level = inverted ? 0 : 1;

    while (gpio_get_level(spi->pin_busy) == busy_level) {
        if (timeout_ms > 0) {
            uint32_t elapsed = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
            if (elapsed >= timeout_ms) {
                ESP_LOGW(TAG, "Wait busy timeout (%lu ms)", timeout_ms);
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
