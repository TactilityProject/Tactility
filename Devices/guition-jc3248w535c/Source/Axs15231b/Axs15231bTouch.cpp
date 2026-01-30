#include "Axs15231bTouch.h"

#include <esp_lcd_axs15231b.h>
#include <esp_err.h>

bool Axs15231bTouch::createIoHandle(esp_lcd_panel_io_handle_t& outHandle) {
    if (configuration == nullptr) {
        return false;
    }
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG();
    return esp_lcd_new_panel_io_i2c(configuration->port, &io_config, &outHandle) == ESP_OK;
}

bool Axs15231bTouch::createTouchHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_touch_config_t& configuration, esp_lcd_touch_handle_t& panelHandle) {
    return esp_lcd_touch_new_i2c_axs15231b(ioHandle, &configuration, &panelHandle) == ESP_OK;
}

esp_lcd_touch_config_t Axs15231bTouch::createEspLcdTouchConfig() {
    return {
        .x_max = configuration->xMax,
        .y_max = configuration->yMax,
        .rst_gpio_num = configuration->pinReset,
        .int_gpio_num = configuration->pinInterrupt,
        .levels = {
            .reset = configuration->pinResetLevel,
            .interrupt = configuration->pinInterruptLevel,
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
