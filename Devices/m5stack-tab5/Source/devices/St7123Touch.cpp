#include "St7123Touch.h"

#include <Tactility/Logger.h>
#include <esp_lcd_touch_st7123.h>
#include <esp_err.h>

static const auto LOGGER = tt::Logger("ST7123Touch");

bool St7123Touch::createIoHandle(esp_lcd_panel_io_handle_t& outHandle) {
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG();
    return esp_lcd_new_panel_io_i2c(
        static_cast<esp_lcd_i2c_bus_handle_t>(configuration->port),
        &io_config,
        &outHandle
    ) == ESP_OK;
}

bool St7123Touch::createTouchHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_touch_config_t& config, esp_lcd_touch_handle_t& touchHandle) {
    return esp_lcd_touch_new_i2c_st7123(ioHandle, &config, &touchHandle) == ESP_OK;
}

esp_lcd_touch_config_t St7123Touch::createEspLcdTouchConfig() {
    return {
        .x_max = configuration->xMax,
        .y_max = configuration->yMax,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = configuration->pinInterrupt,
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
