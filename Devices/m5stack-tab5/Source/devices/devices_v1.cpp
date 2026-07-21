#include "devices_v1.h"

#include "devices_common.h"
#include "ili9881_init_data.h"

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/log.h>

#include <drivers/gt911.h>
#include <drivers/ili9881c.h>

#include <iterator>
#include <vector>

constexpr auto* TAG = "Tab5";

// Display Device + Config + flattened init bytes live for the process's whole lifetime (this
// board never tears down or re-detects its panel), so plain static storage is fine - no need to
// heap-allocate something the kernel would have to track.
static std::vector<uint8_t> display_init_bytes;
static Ili9881cConfig ili9881c_config {};
static Device display_device {};

static Gt911Config gt911_config {};
static Device gt911_device {};

// GPIO 23 has a pull-up resistor to 3V3 that blocks GT911 touch, and can't be used as the
// controller's own interrupt pin for that reason (see detect.cpp's probe_variant() comment) -
// driving it low directly sinks that pull-up instead.
// See https://github.com/espressif/esp-bsp/blob/ad668c765cbad177495a122181df0a70ff9f8f61/bsp/m5stack_tab5/src/m5stack_tab5.c#L777
static void apply_gt911_int_workaround() {
    Device* gpio0 = nullptr;
    if (device_get_by_name("gpio0", &gpio0) != ERROR_NONE) {
        LOG_W(TAG, "display_detect: gpio0 not found, GT911 INT workaround not applied");
        return;
    }
    auto* int_pin = gpio_descriptor_acquire(gpio0, 23, GPIO_OWNER_GPIO);
    device_put(gpio0);
    if (int_pin == nullptr) {
        LOG_W(TAG, "display_detect: failed to acquire GPIO23 for GT911 INT workaround");
        return;
    }
    gpio_descriptor_set_flags(int_pin, GPIO_FLAG_DIRECTION_OUTPUT | GPIO_FLAG_PULL_UP);
    gpio_descriptor_set_level(int_pin, false);
    gpio_descriptor_release(int_pin);
}

// GT911's touch controller is a kernel driver (goodix,gt911, in gt911-module).
static void create_gt911_touch(Device* i2c0) {
    gt911_device = Device {
        .address = 0,
        .name = "touch0",
        .config = nullptr,
        .parent = nullptr,
        .internal = nullptr,
    };

    gt911_config = Gt911Config {
        .address = 0, // unused: the driver auto-probes both known GT911 addresses regardless
        .x_max = 720,
        .y_max = 1280,
        .swap_xy = false,
        .mirror_x = false,
        .mirror_y = false,
        // Reset is pulsed via io_expander0 (detect.cpp's pulse_display_reset_pins), not a direct SoC GPIO.
        .pin_reset = GPIO_PIN_SPEC_NONE,
        .pin_interrupt = GPIO_PIN_SPEC_NONE,
        .reset_active_high = false,
        .interrupt_active_high = false,
    };
    gt911_device.config = &gt911_config;

    apply_gt911_int_workaround();

    construct_add_start(&gt911_device, i2c0, "goodix,gt911");
}

void tab5_create_devices_v1(Device* i2c0) {
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

    flatten_init_sequence(ili9881_init_data, std::size(ili9881_init_data), display_init_bytes);
    ili9881c_config = Ili9881cConfig {
        .horizontal_resolution = 720,
        .vertical_resolution = 1280,
        .bits_per_pixel = 16,
        .bgr_order = false,
        .invert_color = false,
        .mirror_x = false,
        .mirror_y = false,
        // LCD reset is pulsed via io_expander0 (detect.cpp's pulse_display_reset_pins), not a
        // direct SoC GPIO.
        .pin_reset = GPIO_PIN_SPEC_NONE,
        .reset_active_high = false,
        .ldo_channel = 3,
        .ldo_voltage_mv = 2500,
        .dsi_bus_id = 0,
        .num_data_lanes = 2,
        .lane_bit_rate_mbps = 1000,
        .dpi_clock_freq_mhz = 60,
        .hsync_pulse_width = 40,
        .hsync_back_porch = 140,
        .hsync_front_porch = 40,
        .vsync_pulse_width = 4,
        .vsync_back_porch = 20,
        .vsync_front_porch = 20,
        .num_fbs = 2,
        .use_dma2d = true,
        .disable_lp = false,
        .allow_tearing = true, // matches old lvgl_port_display_dsi_cfg_t.avoid_tearing = 0 (disabled = don't wait)
        .init_sequence = display_init_bytes.data(),
        .init_sequence_length = (uint32_t)display_init_bytes.size(),
        .backlight = backlight,
    };
    display_device.config = &ili9881c_config;

    if (backlight != nullptr) {
        device_put(backlight);
    }

    Device* root = nullptr;
    if (device_get_by_name("/", &root) != ERROR_NONE) {
        LOG_E(TAG, "display_detect: root device not found");
        return;
    }
    bool started = construct_add_start(&display_device, root, "ilitek,ili9881c");
    device_put(root);
    if (!started) {
        return;
    }

    create_gt911_touch(i2c0);
}
