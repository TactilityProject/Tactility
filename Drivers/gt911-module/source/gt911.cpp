// SPDX-License-Identifier: Apache-2.0
#include <drivers/gt911.h>
#include <gt911_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_i2c_master.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/pointer.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_lcd_io_i2c.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_gt911.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdlib>

constexpr auto* TAG = "GT911";
#define GET_CONFIG(device) (static_cast<const Gt911Config*>((device)->config))

struct Gt911Internal {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_touch_handle_t touch_handle;
};

static gpio_num_t pin_or_nc(const GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? GPIO_NUM_NC : static_cast<gpio_num_t>(pin.pin);
}

// Reset polarity is carried by the pin_reset descriptor's ACTIVE_HIGH/ACTIVE_LOW flag, so the
// pin is always pulsed with logical levels here. Some boards also require several toggle cycles
// before the controller starts responding on I2C (e.g. Tulip 4 R11). The esp_lcd_touch_gt911
// driver only performs a single reset, so the pin is pulsed here first
static error_t reset_controller_pin(const GpioPinSpec& pin, uint8_t pulses) {
    if (pulses == 0 || pin.gpio_controller == nullptr) {
        return ERROR_NONE;
    }

    auto* descriptor = gpio_descriptor_acquire(pin.gpio_controller, pin.pin, pin.flags | GPIO_FLAG_DIRECTION_OUTPUT, GPIO_OWNER_GPIO);
    if (descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire reset pin");
        return ERROR_RESOURCE;
    }

    for (uint8_t i = 0; i < pulses; ++i) {
        bool last_pulse = i == pulses - 1;
        error_t error = gpio_descriptor_set_level(descriptor, true);
        if (error == ERROR_NONE) {
            vTaskDelay(pdMS_TO_TICKS(11));
            error = gpio_descriptor_set_level(descriptor, false);
        }
        if (error == ERROR_NONE) {
            vTaskDelay(pdMS_TO_TICKS(last_pulse ? 1000 : 60));
        }
        if (error != ERROR_NONE) {
            LOG_E(TAG, "Failed to pulse reset pin");
            gpio_descriptor_release(descriptor);
            return error;
        }
    }

    gpio_descriptor_release(descriptor);
    return ERROR_NONE;
}

// region Driver lifecycle

// GT911's I2C address depends on the controller's INT pin level at power-up (board-strapped, not
// devicetree-fixed), so both known addresses are probed regardless of the devicetree "reg" hint.
static esp_err_t create_io_handle(Device* parent, esp_lcd_panel_io_handle_t* out_handle) {
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();

    if (i2c_controller_has_device_at_address(parent, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS, pdMS_TO_TICKS(10)) == ERROR_NONE) {
        io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    } else if (i2c_controller_has_device_at_address(parent, ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP, pdMS_TO_TICKS(10)) == ERROR_NONE) {
        io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
    } else {
        LOG_E(TAG, "No GT911 found on I2C bus");
        return ESP_ERR_NOT_FOUND;
    }

    auto* parent_driver = device_get_driver(parent);
    if (driver_is_compatible(parent_driver, "espressif,esp32-i2c-master")) {
        auto bus = esp32_i2c_master_get_bus_handle(parent);
        io_config.scl_speed_hz = esp32_i2c_master_get_clock_frequency(parent);
        return esp_lcd_new_panel_io_i2c(bus, &io_config, out_handle);
    }

    LOG_E(TAG, "Unsupported I2C driver");
    return ESP_ERR_NOT_SUPPORTED;
}

// Applies the per-device affine calibration (offset + per-mille scale) in the esp_lcd_touch
// framework's own coordinate hook, so downstream consumers always receive panel-space coords.
// Called before mirror/swap in esp_lcd_touch_get_coordinates().
static void gt911_process_coordinates(
    esp_lcd_touch_handle_t tp,
    uint16_t* x,
    uint16_t* y,
    uint16_t* strength,
    uint8_t* point_count,
    uint8_t max_point_count
) {
    (void)strength;
    (void)max_point_count;
    auto* config = static_cast<const Gt911Config*>(tp->config.user_data);
    if (config == nullptr) {
        return;
    }

    for (uint8_t i = 0; i < *point_count; i++) {
        int32_t mapped_x = (static_cast<int32_t>(x[i]) + config->x_offset) * config->x_scale / 1000;
        int32_t mapped_y = (static_cast<int32_t>(y[i]) + config->y_offset) * config->y_scale / 1000;
        x[i] = static_cast<uint16_t>(mapped_x < 0 ? 0 : (mapped_x > config->x_max ? config->x_max : mapped_x));
        y[i] = static_cast<uint16_t>(mapped_y < 0 ? 0 : (mapped_y > config->y_max ? config->y_max : mapped_y));
    }
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Gt911Internal*>(malloc(sizeof(Gt911Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = reset_controller_pin(config->pin_reset, config->reset_pulses);
    if (error != ERROR_NONE) {
        free(internal);
        return error;
    }

    esp_err_t ret = create_io_handle(parent, &internal->io_handle);
    if (ret != ESP_OK) {
        free(internal);
        return ERROR_RESOURCE;
    }

    esp_lcd_touch_config_t touch_config = {
        .x_max = config->x_max,
        .y_max = config->y_max,
        .rst_gpio_num = pin_or_nc(config->pin_reset),
        .int_gpio_num = pin_or_nc(config->pin_interrupt),
        // Reset polarity comes from the pin_reset descriptor's ACTIVE_HIGH/ACTIVE_LOW flag; the
        // interrupt line is fixed active-low in hardware.
        .levels = {
            .reset = (config->pin_reset.flags & GPIO_FLAG_ACTIVE_LOW) != 0 ? 0u : 1u,
            .interrupt = 0u,
        },
        .flags = {
            .swap_xy = config->swap_xy ? 1u : 0u,
            .mirror_x = config->mirror_x ? 1u : 0u,
            .mirror_y = config->mirror_y ? 1u : 0u,
        },
        .process_coordinates = gt911_process_coordinates,
        .interrupt_callback = nullptr,
        .user_data = (void*)config,
        .driver_data = nullptr,
    };

    ret = esp_lcd_touch_new_i2c_gt911(internal->io_handle, &touch_config, &internal->touch_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create touch handle: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(internal->io_handle);
        free(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Gt911Internal*>(device_get_driver_data(device));

    // esp_lcd_touch_del() only releases the touch-side resources; the panel IO handle is owned
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

// region PointerApi

static error_t gt911_enter_sleep(Device* device) {
    auto* internal = static_cast<Gt911Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_enter_sleep(internal->touch_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t gt911_exit_sleep(Device* device) {
    auto* internal = static_cast<Gt911Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_exit_sleep(internal->touch_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t gt911_read_data(Device* device, TickType_t timeout) {
    (void)timeout; // esp_lcd_touch_read_data() has no timeout parameter
    auto* internal = static_cast<Gt911Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_read_data(internal->touch_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static bool gt911_get_touched_points(Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    auto* internal = static_cast<Gt911Internal*>(device_get_driver_data(device));
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

static error_t gt911_set_swap_xy(Device* device, bool swap) {
    auto* internal = static_cast<Gt911Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_set_swap_xy(internal->touch_handle, swap) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t gt911_get_swap_xy(Device* device, bool* swap) {
    auto* internal = static_cast<Gt911Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_swap_xy(internal->touch_handle, swap) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t gt911_set_mirror_x(Device* device, bool mirror) {
    auto* internal = static_cast<Gt911Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_set_mirror_x(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t gt911_get_mirror_x(Device* device, bool* mirror) {
    auto* internal = static_cast<Gt911Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_mirror_x(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t gt911_set_mirror_y(Device* device, bool mirror) {
    auto* internal = static_cast<Gt911Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_set_mirror_y(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t gt911_get_mirror_y(Device* device, bool* mirror) {
    auto* internal = static_cast<Gt911Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_mirror_y(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// endregion

static const PointerApi gt911_pointer_api = {
    .enter_sleep = gt911_enter_sleep,
    .exit_sleep = gt911_exit_sleep,
    .read_data = gt911_read_data,
    .get_touched_points = gt911_get_touched_points,
    .set_swap_xy = gt911_set_swap_xy,
    .get_swap_xy = gt911_get_swap_xy,
    .set_mirror_x = gt911_set_mirror_x,
    .get_mirror_x = gt911_get_mirror_x,
    .set_mirror_y = gt911_set_mirror_y,
    .get_mirror_y = gt911_get_mirror_y,
};

Driver gt911_driver = {
    .name = "gt911",
    .compatible = (const char*[]) { "goodix,gt911", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &gt911_pointer_api,
    .device_type = &POINTER_TYPE,
    .owner = &gt911_module,
    .internal = nullptr
};
