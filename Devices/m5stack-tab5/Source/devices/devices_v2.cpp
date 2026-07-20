#include "devices_v2.h"

#include "devices_common.h"
#include "st7123_init_data.h"

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>
#include <tactility/log.h>

#include <drivers/st7123.h>
#include <drivers/st7123_touch.h>

#include <iterator>
#include <vector>

#define TAG "Tab5"

static std::vector<uint8_t> display_init_bytes;
static St7123Config st7123_config {};
static Device display_device {};

static St7123TouchConfig st7123_touch_config {};
static Device st7123_touch_device {};

static void create_st7123_touch(Device* i2c0) {
    st7123_touch_device = Device {
        .address = 0,
        .name = "touch0",
        .config = nullptr,
        .parent = nullptr,
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
        .reset_active_high = false,
        .interrupt_active_high = false,
    };
    st7123_touch_device.config = &st7123_touch_config;

    construct_add_start(&st7123_touch_device, i2c0, "sitronix,st7123-touch");
}

void tab5_create_devices_v2(Device* i2c0) {
    display_device = Device {
        .address = 0,
        .name = "display0",
        .config = nullptr,
        .parent = nullptr,
        .internal = nullptr,
    };

    Device* backlight = nullptr;
    if (device_get_by_name("display_backlight", &backlight) != ERROR_NONE) {
        LOG_W(TAG, "display_detect: display_backlight not found");
    }

    flatten_init_sequence(st7123_init_data, std::size(st7123_init_data), display_init_bytes);
    st7123_config = St7123Config {
        .horizontal_resolution = 720,
        .vertical_resolution = 1280,
        .bits_per_pixel = 16,
        .bgr_order = false,
        .invert_color = false,
        .mirror_x = false,
        .mirror_y = false,
        .pin_reset = GPIO_PIN_SPEC_NONE,
        .reset_active_high = false,
        .ldo_channel = 3,
        .ldo_voltage_mv = 2500,
        .dsi_bus_id = 0,
        .num_data_lanes = 2,
        .lane_bit_rate_mbps = 965, // ST7123 lane bitrate per M5Stack BSP
        .dpi_clock_freq_mhz = 70,
        .hsync_pulse_width = 2,
        .hsync_back_porch = 40,
        .hsync_front_porch = 40,
        .vsync_pulse_width = 2,
        .vsync_back_porch = 8,
        .vsync_front_porch = 220,
        .num_fbs = 1,
        .use_dma2d = false,
        .disable_lp = false,
        .allow_tearing = true,
        .init_sequence = display_init_bytes.data(),
        .init_sequence_length = (uint32_t)display_init_bytes.size(),
        .backlight = backlight,
    };
    display_device.config = &st7123_config;

    if (backlight != nullptr) {
        device_put(backlight);
    }

    Device* root = nullptr;
    if (device_get_by_name("/", &root) != ERROR_NONE) {
        LOG_E(TAG, "display_detect: root device not found");
        return;
    }
    bool started = construct_add_start(&display_device, root, "sitronix,st7123");
    device_put(root);
    if (!started) {
        return;
    }

    create_st7123_touch(i2c0);
}
