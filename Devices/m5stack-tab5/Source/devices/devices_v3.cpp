#include "devices_v3.h"

#include "devices_common.h"
#include "devices_v2_v3_touch.h"

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>
#include <tactility/log.h>

#include <drivers/st7121.h>

#define TAG "Tab5"

static St7121Config st7121_config {};
static Device display_device {};

// Newest Tab5 variant: ST7121 display + the same in-cell ST7123 touch controller as V2 (see
// devices_common.cpp's create_st7123_touch). No custom init-sequence is supplied - unlike ST7123,
// the M5Tab5 UserDemo runs the ST7121 panel with the esp_lcd_st7121 component's own built-in
// default bring-up sequence (see esp_lcd_st7121.c's vendor_specific_init_default), so
// init_sequence stays null here too. Timing values (vsync_pulse_width/back_porch/front_porch) are
// per the UserDemo's is_st7121 branch in bsp_display_new_with_handles_to_st7123() - the only
// values that differ from V2/ST7123.
void tab5_create_devices_v3(Device* i2c0) {
    display_device = Device {
        .address = 0,
        .name = "display0",
        .config = nullptr,
        .parent = nullptr,
        .flags = {},
        .internal = nullptr,
    };

    Device* backlight = nullptr;
    if (device_get_by_name("display_backlight", &backlight) != ERROR_NONE) {
        LOG_W(TAG, "display_detect: display_backlight not found");
    }

    st7121_config = St7121Config {
        .horizontal_resolution = 720,
        .vertical_resolution = 1280,
        .bits_per_pixel = 16,
        .bgr_order = false,
        .invert_color = false,
        .mirror_x = false,
        .mirror_y = false,
        .pin_reset = GPIO_PIN_SPEC_NONE,
        .ldo_channel = 3,
        .ldo_voltage_mv = 2500,
        .dsi_bus_id = 0,
        .num_data_lanes = 2,
        .lane_bit_rate_mbps = 965, // ST7121 lane bitrate per M5Stack BSP (same as ST7123)
        .dpi_clock_freq_mhz = 70,
        .hsync_pulse_width = 2,
        .hsync_back_porch = 40,
        .hsync_front_porch = 40,
        .vsync_pulse_width = 20,
        .vsync_back_porch = 24,
        .vsync_front_porch = 200,
        .num_fbs = 2,
        .use_dma2d = true,
        .disable_lp = false,
        .allow_tearing = true, // matches old lvgl_port_display_dsi_cfg_t.avoid_tearing = 0 (disabled = don't wait)
        .init_sequence = nullptr, // use esp_lcd_st7121's built-in default sequence, per the UserDemo
        .init_sequence_length = 0,
        .backlight = backlight,
    };
    display_device.config = &st7121_config;

    if (backlight != nullptr) {
        device_put(backlight);
    }

    Device* root = nullptr;
    if (device_get_by_name("/", &root) != ERROR_NONE) {
        LOG_E(TAG, "display_detect: root device not found");
        return;
    }
    bool started = construct_add_start(&display_device, root, "sitronix,st7121");
    device_put(root);
    if (!started) {
        return;
    }

    create_st7123_touch(i2c0);
}
