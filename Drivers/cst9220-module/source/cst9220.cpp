// SPDX-License-Identifier: Apache-2.0
#include <drivers/cst9220.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_i2c_master.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/touch.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_lcd_io_i2c.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_cst9220.h>

#include <cstdlib>

#define TAG "CST9220"
#define GET_CONFIG(device) (static_cast<const Cst9220Config*>((device)->config))

struct Cst9220Internal {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_touch_handle_t touch_handle;
};

static inline gpio_num_t pin_or_nc(const struct GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? GPIO_NUM_NC : static_cast<gpio_num_t>(pin.pin);
}

// region Driver lifecycle

static esp_err_t create_io_handle(Device* parent, uint8_t address, esp_lcd_panel_io_handle_t* out_handle) {
    // Not using ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG(): its designated-initializer field order
    // predates this SDK's esp_lcd_panel_io_i2c_config_t layout.
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = address,
        .scl_speed_hz = 400000,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .flags = {
            .dc_low_on_data = 0,
            .disable_control_phase = 1,
        },
        // Left at 0 the transfer waits forever, and it runs under the LVGL lock.
        .transaction_timeout_ms = 100,
    };

    auto* parent_driver = device_get_driver(parent);
    if (driver_is_compatible(parent_driver, "espressif,esp32-i2c-master")) {
        auto bus = esp32_i2c_master_get_bus_handle(parent);
        io_config.scl_speed_hz = esp32_i2c_master_get_clock_frequency(parent);
        return esp_lcd_new_panel_io_i2c(bus, &io_config, out_handle);
    }

    LOG_E(TAG, "Unsupported I2C driver");
    return ESP_ERR_NOT_SUPPORTED;
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Cst9220Internal*>(malloc(sizeof(Cst9220Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    esp_err_t ret = create_io_handle(parent, config->address, &internal->io_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        free(internal);
        return ERROR_RESOURCE;
    }

    esp_lcd_touch_config_t touch_config = {
        .x_max = config->x_max,
        .y_max = config->y_max,
        .rst_gpio_num = pin_or_nc(config->pin_reset),
        .int_gpio_num = pin_or_nc(config->pin_interrupt),
        // CST9220's interrupt line is active-low in hardware. Reset is usually not wired.
        .levels = {
            .reset = 0u,
            .interrupt = 0u,
        },
        .flags = {
            .swap_xy = config->swap_xy ? 1u : 0u,
            .mirror_x = config->mirror_x ? 1u : 0u,
            .mirror_y = config->mirror_y ? 1u : 0u,
        },
        .process_coordinates = nullptr,
        .interrupt_callback = nullptr,
        .user_data = nullptr,
        .driver_data = nullptr,
    };

    ret = esp_lcd_touch_new_i2c_cst9220(internal->io_handle, &touch_config, &internal->touch_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create touch handle: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(internal->io_handle);
        free(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);

    // An idle CST9217 returns 0xFF for every byte of the report register, which the shared
    // esp_lcd_touch parser reports as a malformed header on every poll. That case is benign and
    // otherwise floods the console at the polling rate, so only keep real errors from that tag.
    esp_log_level_set("cst9220", ESP_LOG_ERROR);

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Cst9220Internal*>(device_get_driver_data(device));

    // esp_lcd_touch_del() only releases the touch-side resources. The panel IO handle is owned
    // separately and needs its own deletion.
    if (internal->touch_handle != nullptr) {
        if (esp_lcd_touch_del(internal->touch_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete touch handle");
            return ERROR_RESOURCE;
        }
        internal->touch_handle = nullptr;
    }

    if (internal->io_handle != nullptr) {
        if (esp_lcd_panel_io_del(internal->io_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete panel IO handle");
            return ERROR_RESOURCE;
        }
        internal->io_handle = nullptr;
    }

    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region TouchApi

static error_t cst9220_enter_sleep(Device* device) {
    auto* internal = static_cast<Cst9220Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_enter_sleep(internal->touch_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t cst9220_exit_sleep(Device* device) {
    auto* internal = static_cast<Cst9220Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_exit_sleep(internal->touch_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t cst9220_read_data(Device* device, TickType_t timeout) {
    (void)timeout; // esp_lcd_touch_read_data() has no timeout parameter
    auto* internal = static_cast<Cst9220Internal*>(device_get_driver_data(device));
    // The controller's INT line pulses rather than staying asserted, so gating reads on its level
    // makes touches get missed. Read unconditionally. The shared parser's benign "malformed
    // header" warning for 0xFF idle reads is silenced in start().
    return esp_lcd_touch_read_data(internal->touch_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static bool cst9220_get_touched_points(Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    auto* internal = static_cast<Cst9220Internal*>(device_get_driver_data(device));
    esp_lcd_touch_point_data_t points[CONFIG_ESP_LCD_TOUCH_MAX_POINTS];
    uint8_t clamped_max_point_count = max_point_count < CONFIG_ESP_LCD_TOUCH_MAX_POINTS ? max_point_count : CONFIG_ESP_LCD_TOUCH_MAX_POINTS;
    if (esp_lcd_touch_get_data(internal->touch_handle, points, point_count, clamped_max_point_count) != ESP_OK) {
        return false;
    }
    for (uint8_t i = 0; i < *point_count; i++) {
        x[i] = points[i].x;
        y[i] = points[i].y;
        if (strength != nullptr) {
            strength[i] = points[i].strength;
        }
    }
    return *point_count > 0;
}

static error_t cst9220_set_swap_xy(Device* device, bool swap) {
    auto* internal = static_cast<Cst9220Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_set_swap_xy(internal->touch_handle, swap) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t cst9220_get_swap_xy(Device* device, bool* swap) {
    auto* internal = static_cast<Cst9220Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_swap_xy(internal->touch_handle, swap) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t cst9220_set_mirror_x(Device* device, bool mirror) {
    auto* internal = static_cast<Cst9220Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_set_mirror_x(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t cst9220_get_mirror_x(Device* device, bool* mirror) {
    auto* internal = static_cast<Cst9220Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_mirror_x(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t cst9220_set_mirror_y(Device* device, bool mirror) {
    auto* internal = static_cast<Cst9220Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_set_mirror_y(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t cst9220_get_mirror_y(Device* device, bool* mirror) {
    auto* internal = static_cast<Cst9220Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_mirror_y(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// endregion

static const TouchApi cst9220_touch_api = {
    .enter_sleep = cst9220_enter_sleep,
    .exit_sleep = cst9220_exit_sleep,
    .read_data = cst9220_read_data,
    .get_touched_points = cst9220_get_touched_points,
    .set_swap_xy = cst9220_set_swap_xy,
    .get_swap_xy = cst9220_get_swap_xy,
    .set_mirror_x = cst9220_set_mirror_x,
    .get_mirror_x = cst9220_get_mirror_x,
    .set_mirror_y = cst9220_set_mirror_y,
    .get_mirror_y = cst9220_get_mirror_y,
};

Driver cst9220_driver = {
    .name = "cst9220",
    .compatible = (const char*[]) { "hynitron,cst9220", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &cst9220_touch_api,
    .device_type = &TOUCH_TYPE,
    .internal = nullptr
};
