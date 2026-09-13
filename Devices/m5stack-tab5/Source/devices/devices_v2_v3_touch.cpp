#include "devices_v2_v3_touch.h"

#include "devices_common.h"

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>
#include <tactility/log.h>

#include <drivers/st7123_touch.h>

constexpr auto* TAG = "Tab5";

static St7123TouchConfig st7123_touch_config {};
static Device st7123_touch_device {};

void create_st7123_touch(Device* i2c0) {
    st7123_touch_device = Device {
        .address = 0,
        .name = "touch0",
        .config = nullptr,
        .parent = nullptr,
        .flags = {},
        .internal = nullptr,
    };

    GpioPinSpec pin_interrupt = GPIO_PIN_SPEC_NONE;
    Device* gpio0 = nullptr;
    if (device_get_by_name("gpio0", &gpio0) == ERROR_NONE) {
        pin_interrupt = GpioPinSpec { gpio0, 23, GPIO_FLAG_NONE };
        device_put(gpio0);
    } else {
        LOG_W(TAG, "display_detect: gpio0 not found, touch interrupt pin will not be wired");
    }

    st7123_touch_config = St7123TouchConfig {
        .address = 0x55, // fixed - see ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS
        .x_max = 720,
        .y_max = 1280,
        .swap_xy = false,
        .mirror_x = false,
        .mirror_y = false,
        // Reset is pulsed via io_expander0 (detect.cpp's pulse_display_reset_pins), not a direct SoC GPIO.
        .pin_reset = GPIO_PIN_SPEC_NONE,
        .pin_interrupt = pin_interrupt,
    };
    st7123_touch_device.config = &st7123_touch_config;

    construct_add_start(&st7123_touch_device, i2c0, "sitronix,st7123-touch");
}
