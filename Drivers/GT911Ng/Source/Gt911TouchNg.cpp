#include "Gt911TouchNg.h"

#include <Tactility/Logger.h>
#include <tactility/drivers/esp32_i2c_master.h>
#include <tactility/drivers/i2c_controller.h>

#include <esp_lcd_touch_gt911.h>
#include <esp_err.h>

static const auto LOGGER = tt::Logger("GT911");

bool Gt911TouchNg::createIoHandle(esp_lcd_panel_io_handle_t& outHandle) {
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_config.scl_speed_hz = esp32_i2c_master_get_clock_frequency(configuration->controller);
    i2c_master_bus_handle_t bus = esp32_i2c_master_get_bus_handle(configuration->controller);

    /**
     * When the interrupt pin is low, the address is 0x5D. Otherwise it is 0x14.
     * There is not reset pin, and the current driver fails when you only specify the interrupt pin.
     * Because of that, we don't use the interrupt pin and we'll simply scan the bus instead:
     */
    if (i2c_controller_has_device_at_address(configuration->controller, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, pdMS_TO_TICKS(100))) {
        io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    } else if (i2c_controller_has_device_at_address(configuration->controller, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, pdMS_TO_TICKS(100))) {
        io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
    } else {
        LOGGER.error("No device found on I2C bus");
        return false;
    }

    return esp_lcd_new_panel_io_i2c_v2(bus, &io_config, &outHandle) == ESP_OK;
}

bool Gt911TouchNg::createTouchHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_touch_config_t& configuration, esp_lcd_touch_handle_t& panelHandle) {
    return esp_lcd_touch_new_i2c_gt911(ioHandle, &configuration, &panelHandle) == ESP_OK;
}

esp_lcd_touch_config_t Gt911TouchNg::createEspLcdTouchConfig() {
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
