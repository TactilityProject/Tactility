#include "detect.h"

#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#include <esp_lcd_touch_gt911.h>
#include <esp_lcd_touch_st7123.h>

#include <freertos/FreeRTOS.h>

constexpr auto* TAG = "Tab5";

static Tab5Variant detected_variant = Tab5Variant::Unknown;

Tab5Variant tab5_get_variant() {
    return detected_variant;
}

void tab5_set_variant(Tab5Variant variant) {
    detected_variant = variant;
}

Tab5Variant tab5_probe_variant(Device* i2c0) {
    // Allow time for the touch IC to fully boot after the reset pulse above: 100ms is enough for
    // I2C ACK (probe) but cold power-on needs ~300ms before register reads succeed reliably.
    vTaskDelay(pdMS_TO_TICKS(300));

    constexpr auto PROBE_TIMEOUT = pdMS_TO_TICKS(50);

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (i2c_controller_has_device_at_address(i2c0, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, PROBE_TIMEOUT) == ERROR_NONE ||
            i2c_controller_has_device_at_address(i2c0, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, PROBE_TIMEOUT) == ERROR_NONE) {
            LOG_I(TAG, "display_detect: detected GT911 touch — using variant V1 ");
            return Tab5Variant::V1;
        }

        if (i2c_controller_has_device_at_address(i2c0, ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS, PROBE_TIMEOUT) == ERROR_NONE) {
            LOG_I(TAG, "display_detect: detected ST7123 touch — using variant V2");
            return Tab5Variant::V2;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    LOG_W(TAG, "display_detect: no known touch controller detected, defaulting to V2");
    return Tab5Variant::V2;
}
