#include "Detect.h"

#include <Tactility/Logger.h>
#include <Tactility/hal/i2c/I2c.h>
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

    for (int attempt = 0; attempt < 3; ++attempt) {
        // GT911 address depends on INT pin state during reset:
        //   GPIO 23 has a pull-up resistor to 3V3, so INT is high at reset → GT911 uses 0x5D (primary)
        //   It may also appear at 0x14 (backup) if the pin happened to be driven low
        if (tt::hal::i2c::masterHasDeviceAtAddress(I2C_NUM_0, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS) ||
            tt::hal::i2c::masterHasDeviceAtAddress(I2C_NUM_0, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP)) {
            LOGGER.info("Detected GT911 touch — using ILI9881C display");
            return Tab5Variant::Ili9881c_Gt911;
        }

        // Probe for ST7123 touch (new variant)
        if (tt::hal::i2c::masterHasDeviceAtAddress(I2C_NUM_0, ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS)) {
            LOGGER.info("Detected ST7123 touch — using ST7123 display");
            return Tab5Variant::St7123;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    LOGGER.warn("No known touch controller detected, defaulting to ST7123");
    return Tab5Variant::St7123;
}
