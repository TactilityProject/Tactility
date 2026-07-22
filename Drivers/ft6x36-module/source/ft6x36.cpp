// SPDX-License-Identifier: Apache-2.0
#include <drivers/ft6x36.h>

#include <ft6x36_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_i2c.h>
#include <tactility/drivers/esp32_i2c_master.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/pointer.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_lcd_io_i2c.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_ft6x36.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdlib>

#define TAG "FT6x36"
#define GET_CONFIG(device) (static_cast<const Ft6x36Config*>((device)->config))

struct Ft6x36Internal {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_touch_handle_t touch_handle;
    // Non-null when pin_reset is configured. Owned/pulsed by this driver instead of esp_lcd_touch
    GpioDescriptor* reset_descriptor;
};

// Only valid for pin_interrupt: esp_lcd_touch only ever reads this pin's level / attaches an ISR
// to it via ESP-IDF's native gpio_* calls, so - like ili9341-module's cs/dc pins - it must be a
// real ESP32 GPIO. pin_reset has no such requirement and must never go through this helper; see
// pulse_reset() instead.
static inline gpio_num_t pin_or_nc(const struct GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? GPIO_NUM_NC : static_cast<gpio_num_t>(pin.pin);
}

// See ili9341-module's pulse_reset() for the full rationale; same idea, same 10ms/10ms timing.
// esp_lcd_touch's rst_gpio_num is always left at GPIO_NUM_NC (see start()), under which it just
// skips its own reset step entirely.
static error_t pulse_reset(GpioDescriptor* descriptor) {
    if (descriptor == nullptr) {
        return ERROR_NONE;
    }
    // Logical high (physical low, because of earlier GPIO_FLAG_ACTIVE_LOW)
    error_t error = gpio_descriptor_set_level(descriptor, true);
    if (error != ERROR_NONE) {
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    // Logical low (physical high, because of earlier GPIO_FLAG_ACTIVE_LOW)
    error = gpio_descriptor_set_level(descriptor, false);
    if (error != ERROR_NONE) {
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(300));
    return ERROR_NONE;
}

// region Driver lifecycle

// FT6x36 always sits at a fixed I2C address, unlike GT911's strapping-dependent address,
// so no bus probing is needed here.
static esp_err_t create_io_handle(Device* parent, esp_lcd_panel_io_handle_t* out_handle) {
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_FT6x36_CONFIG();

    auto* parent_driver = device_get_driver(parent);
    if (driver_is_compatible(parent_driver, "espressif,esp32-i2c")) {
        auto port = static_cast<const Esp32I2cConfig*>(parent->config)->port;
        return esp_lcd_new_panel_io_i2c_v1(port, &io_config, out_handle);
    }
    if (driver_is_compatible(parent_driver, "espressif,esp32-i2c-master")) {
        auto bus = esp32_i2c_master_get_bus_handle(parent);
        io_config.scl_speed_hz = esp32_i2c_master_get_clock_frequency(parent);
        return esp_lcd_new_panel_io_i2c_v2(bus, &io_config, out_handle);
    }

    LOG_E(TAG, "Unsupported I2C driver");
    return ESP_ERR_NOT_SUPPORTED;
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Ft6x36Internal*>(malloc(sizeof(Ft6x36Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->reset_descriptor = nullptr;
    if (config->pin_reset.gpio_controller != nullptr) {
        internal->reset_descriptor = gpio_descriptor_acquire(config->pin_reset.gpio_controller, config->pin_reset.pin, GPIO_FLAG_DIRECTION_OUTPUT | GPIO_FLAG_ACTIVE_LOW, GPIO_OWNER_GPIO);
        if (internal->reset_descriptor == nullptr) {
            LOG_E(TAG, "Failed to acquire reset GPIO descriptor");
            free(internal);
            return ERROR_RESOURCE;
        }
    }

    esp_err_t ret = create_io_handle(parent, &internal->io_handle);
    if (ret != ESP_OK) {
        if (internal->reset_descriptor != nullptr) {
            gpio_descriptor_release(internal->reset_descriptor);
        }
        free(internal);
        return ERROR_RESOURCE;
    }

    if (pulse_reset(internal->reset_descriptor) != ERROR_NONE) {
        LOG_E(TAG, "Failed to pulse reset pin");
        esp_lcd_panel_io_del(internal->io_handle);
        if (internal->reset_descriptor != nullptr) {
            gpio_descriptor_release(internal->reset_descriptor);
        }
        free(internal);
        return ERROR_RESOURCE;
    }

    esp_lcd_touch_config_t touch_config = {
        .x_max = config->x_max,
        .y_max = config->y_max,
        // Always NC: pulse_reset() above already handled the physical pin (see its comment for
        // why); esp_lcd_touch just skips its own no-op reset step when this is NC.
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = pin_or_nc(config->pin_interrupt),
        // FT6x36's reset and interrupt lines are both fixed active-low in hardware.
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

    ret = esp_lcd_touch_new_i2c_ft6x36(internal->io_handle, &touch_config, &internal->touch_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create touch handle: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(internal->io_handle);
        if (internal->reset_descriptor != nullptr) {
            gpio_descriptor_release(internal->reset_descriptor);
        }
        free(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Ft6x36Internal*>(device_get_driver_data(device));

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

    if (internal->reset_descriptor != nullptr) {
        gpio_descriptor_release(internal->reset_descriptor);
        internal->reset_descriptor = nullptr;
    }

    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region PointerApi

static error_t ft6x36_enter_sleep(Device* device) {
    auto* internal = static_cast<Ft6x36Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_enter_sleep(internal->touch_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ft6x36_exit_sleep(Device* device) {
    auto* internal = static_cast<Ft6x36Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_exit_sleep(internal->touch_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ft6x36_read_data(Device* device, TickType_t timeout) {
    (void)timeout; // esp_lcd_touch_read_data() has no timeout parameter
    auto* internal = static_cast<Ft6x36Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_read_data(internal->touch_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static bool ft6x36_get_touched_points(Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    auto* internal = static_cast<Ft6x36Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_coordinates(internal->touch_handle, x, y, strength, point_count, max_point_count);
}

static error_t ft6x36_set_swap_xy(Device* device, bool swap) {
    auto* internal = static_cast<Ft6x36Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_set_swap_xy(internal->touch_handle, swap) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ft6x36_get_swap_xy(Device* device, bool* swap) {
    auto* internal = static_cast<Ft6x36Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_swap_xy(internal->touch_handle, swap) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ft6x36_set_mirror_x(Device* device, bool mirror) {
    auto* internal = static_cast<Ft6x36Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_set_mirror_x(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ft6x36_get_mirror_x(Device* device, bool* mirror) {
    auto* internal = static_cast<Ft6x36Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_mirror_x(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ft6x36_set_mirror_y(Device* device, bool mirror) {
    auto* internal = static_cast<Ft6x36Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_set_mirror_y(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ft6x36_get_mirror_y(Device* device, bool* mirror) {
    auto* internal = static_cast<Ft6x36Internal*>(device_get_driver_data(device));
    return esp_lcd_touch_get_mirror_y(internal->touch_handle, mirror) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// endregion

static const PointerApi ft6x36_pointer_api = {
    .enter_sleep = ft6x36_enter_sleep,
    .exit_sleep = ft6x36_exit_sleep,
    .read_data = ft6x36_read_data,
    .get_touched_points = ft6x36_get_touched_points,
    .set_swap_xy = ft6x36_set_swap_xy,
    .get_swap_xy = ft6x36_get_swap_xy,
    .set_mirror_x = ft6x36_set_mirror_x,
    .get_mirror_x = ft6x36_get_mirror_x,
    .set_mirror_y = ft6x36_set_mirror_y,
    .get_mirror_y = ft6x36_get_mirror_y,
};

Driver ft6x36_driver = {
    .name = "ft6x36",
    .compatible = (const char*[]) { "focaltech,ft6x36", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &ft6x36_pointer_api,
    .device_type = &POINTER_TYPE,
    .owner = &ft6x36_module,
    .internal = nullptr
};
