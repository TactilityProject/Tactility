#include "Xpt2046Touch.h"

#include <Tactility/lvgl/LvglSync.h>

#include <esp_err.h>
#include <esp_lcd_touch_xpt2046.h>

bool Xpt2046Touch::createIoHandle(esp_lcd_panel_io_handle_t& outHandle) {
    const esp_lcd_panel_io_spi_config_t io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(configuration->spiPinCs);
    return esp_lcd_new_panel_io_spi(configuration->spiDevice, &io_config, &outHandle) == ESP_OK;
}

bool Xpt2046Touch::createTouchHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_touch_config_t& config, esp_lcd_touch_handle_t& panelHandle) {
    return esp_lcd_touch_new_spi_xpt2046(ioHandle, &config, &panelHandle) == ESP_OK;
}

esp_lcd_touch_config_t Xpt2046Touch::createEspLcdTouchConfig() {
   return {
        .x_max = configuration->xMax,
        .y_max = configuration->yMax,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = configuration->swapXy,
            .mirror_x = configuration->mirrorX,
            .mirror_y = configuration->mirrorY,
        },
        .process_coordinates = nullptr,
        .interrupt_callback = nullptr,
        .user_data = nullptr,
        .driver_data = nullptr
    };
}
