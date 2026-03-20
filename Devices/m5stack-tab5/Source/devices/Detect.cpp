#include "Detect.h"

#include <Tactility/Logger.h>
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <esp_lcd_touch_gt911.h>
#include <esp_lcd_touch_st7123.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const auto LOGGER = tt::Logger("Tab5Detect");

Tab5Variant detectVariant() {
    // Allow time for touch IC to fully boot after expander reset in initBoot().
    // 100ms is enough for I2C ACK (probe) but cold power-on needs ~300ms before
    // register reads (read_fw_info) succeed reliably.
    vTaskDelay(pdMS_TO_TICKS(300));

    auto* i2c0 = device_find_by_name("i2c0");
    check(i2c0);

    constexpr auto PROBE_TIMEOUT = pdMS_TO_TICKS(50);

    for (int attempt = 0; attempt < 3; ++attempt) {
        // GT911 address depends on INT pin state during reset:
        //   GPIO 23 has a pull-up resistor to 3V3, so INT is high at reset → GT911 uses 0x5D (primary)
        //   It may also appear at 0x14 (backup) if the pin happened to be driven low
        if (i2c_controller_has_device_at_address(i2c0, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, PROBE_TIMEOUT) == ERROR_NONE ||
            i2c_controller_has_device_at_address(i2c0, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, PROBE_TIMEOUT) == ERROR_NONE) {
            LOGGER.info("Detected GT911 touch — using ILI9881C display");
            return Tab5Variant::Ili9881c_Gt911;
        }

        // Probe for ST7123 touch (new variant)
        if (i2c_controller_has_device_at_address(i2c0, ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS, PROBE_TIMEOUT) == ERROR_NONE) {
            LOGGER.info("Detected ST7123 touch — using ST7123 display");
            return Tab5Variant::St7123;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    LOGGER.warn("No known touch controller detected, defaulting to ST7123");
    return Tab5Variant::St7123;
}
