#include "Gt911Touch.h"

#include <Tactility/Logger.h>

#include <esp_lcd_io_i2c.h>
#include <esp_lcd_touch_gt911.h>
#include <esp_err.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_i2c.h>
#include <tactility/drivers/esp32_i2c_master.h>
#include <tactility/drivers/i2c_controller.h>

static const auto LOGGER = tt::Logger("GT911");

bool Gt911Touch::createIoHandle(esp_lcd_panel_io_handle_t& outHandle) {
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();

    auto* i2c = configuration->i2cController;

    if (i2c_controller_has_device_at_address(i2c, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, pdMS_TO_TICKS(10)) == ERROR_NONE) {
        io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    } else if (i2c_controller_has_device_at_address(i2c, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, pdMS_TO_TICKS(10)) == ERROR_NONE) {
        io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
    } else {
        LOGGER.error("No device found on I2C bus");
        return false;
    }

    io_config.scl_speed_hz = esp32_i2c_master_get_clock_frequency(configuration->i2cController);

    auto* driver = device_get_driver(i2c);
    if (driver_is_compatible(driver, "espressif,esp32-i2c")) {
        auto port = static_cast<const Esp32I2cConfig*>(i2c->config)->port;
        return esp_lcd_new_panel_io_i2c_v1(port, &io_config, &outHandle) == ESP_OK;
    } else if (driver_is_compatible(driver, "espressif,esp32-i2c-master")) {
        auto bus = esp32_i2c_master_get_bus_handle(i2c);
        return esp_lcd_new_panel_io_i2c_v2(bus, &io_config, &outHandle) == ESP_OK;
    }

    LOGGER.error("Unsupported I2C driver");
    return false;
}

bool Gt911Touch::createTouchHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_touch_config_t& configuration, esp_lcd_touch_handle_t& panelHandle) {
    return esp_lcd_touch_new_i2c_gt911(ioHandle, &configuration, &panelHandle) == ESP_OK;
}

esp_lcd_touch_config_t Gt911Touch::createEspLcdTouchConfig() {
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
