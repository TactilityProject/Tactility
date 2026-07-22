// SPDX-License-Identifier: Apache-2.0
#include <drivers/axs5106.h>

#include <axs5106_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/pointer.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdlib>

constexpr auto* TAG = "AXS5106";
#define GET_CONFIG(device) (static_cast<const Axs5106Config*>((device)->config))

// Register map and read/reset logic ported from the vendor factory demo
// (ESP32-S3-Touch-LCD-1.47-Demo/ESP-IDF/01_factory/components/esp_lcd_touch_axs5106), which talks
// to the chip directly over I2C rather than through esp_lcd_panel_io/esp_lcd_touch - that indirection
// is what the previous version of this driver used, and reads through it consistently failed with
// ESP_FAIL on real hardware.
static constexpr uint8_t REG_TOUCH_POINTS = 0x01;
static constexpr uint8_t TOUCH_DATA_LEN = 14;
static constexpr uint8_t MAX_POINTS = 2;

struct Axs5106Internal {
    GpioDescriptor* reset_descriptor;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    uint8_t point_count;
    uint16_t x[MAX_POINTS];
    uint16_t y[MAX_POINTS];
};

// region Driver lifecycle

static error_t reset_pulse(GpioDescriptor* descriptor) {
    bool ok = gpio_descriptor_set_level(descriptor, true) == ERROR_NONE;
    vTaskDelay(pdMS_TO_TICKS(10));
    ok = ok && gpio_descriptor_set_level(descriptor, false) == ERROR_NONE;
    vTaskDelay(pdMS_TO_TICKS(10));

    return ok ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Axs5106Internal*>(malloc(sizeof(Axs5106Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->reset_descriptor = nullptr;
    internal->swap_xy = config->swap_xy;
    internal->mirror_x = config->mirror_x;
    internal->mirror_y = config->mirror_y;
    internal->point_count = 0;

    if (config->pin_reset.gpio_controller != nullptr) {
        internal->reset_descriptor = gpio_descriptor_acquire(config->pin_reset.gpio_controller, config->pin_reset.pin, GPIO_FLAG_DIRECTION_OUTPUT | GPIO_FLAG_ACTIVE_LOW, GPIO_OWNER_GPIO);
        if (internal->reset_descriptor == nullptr) {
            LOG_E(TAG, "Failed to acquire reset pin");
            free(internal);
            return ERROR_RESOURCE;
        }

        if (reset_pulse(internal->reset_descriptor) != ERROR_NONE) {
            LOG_E(TAG, "Failed to pulse reset pin");
            gpio_descriptor_release(internal->reset_descriptor);
            free(internal);
            return ERROR_RESOURCE;
        }
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Axs5106Internal*>(device_get_driver_data(device));

    if (internal->reset_descriptor != nullptr) {
        gpio_descriptor_release(internal->reset_descriptor);
    }

    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region PointerApi

static error_t axs5106_read_data(Device* device, TickType_t) {
    auto* internal = static_cast<Axs5106Internal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);
    auto* parent = device_get_parent(device);

    // Matches the vendor demo's touch_axs5106_i2c_read(): the register address write and the
    // data read are two separate transactions (i2c_master_transmit() then i2c_master_receive()),
    // not a single write-then-read transaction with a repeated start.
    uint8_t reg = REG_TOUCH_POINTS;
    error_t error = i2c_controller_write(parent, config->address, &reg, 1, pdMS_TO_TICKS(config->transmit_timeout_ms));
    if (error != ERROR_NONE) {
        return error;
    }

    uint8_t data[TOUCH_DATA_LEN];
    error = i2c_controller_read(parent, config->address, data, sizeof(data), pdMS_TO_TICKS(config->receive_timeout_ms));
    if (error != ERROR_NONE) {
        return error;
    }

    uint8_t points = data[1] & 0x0F;
    if (points > MAX_POINTS) {
        points = MAX_POINTS;
    }
    internal->point_count = points;

    for (uint8_t i = 0; i < points; i++) {
        internal->y[i] = static_cast<uint16_t>(((data[4 + i * 6] & 0x0F) << 8) | data[5 + i * 6]);
        internal->x[i] = static_cast<uint16_t>(((data[2 + i * 6] & 0x0F) << 8) | data[3 + i * 6]);
    }

    return ERROR_NONE;
}

static bool axs5106_get_touched_points(Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    auto* internal = static_cast<Axs5106Internal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);

    uint8_t count = internal->point_count;
    if (count > max_point_count) {
        count = max_point_count;
    }

    // Order matches esp_lcd_touch's own software coordinate adjustment (mirror X, mirror Y,
    // then swap) so behavior lines up with the other touch modules in this codebase.
    for (uint8_t i = 0; i < count; i++) {
        uint16_t point_x = internal->x[i];
        uint16_t point_y = internal->y[i];
        if (internal->mirror_x) {
            point_x = config->x_max - point_x;
        }
        if (internal->mirror_y) {
            point_y = config->y_max - point_y;
        }
        if (internal->swap_xy) {
            uint16_t tmp = point_x;
            point_x = point_y;
            point_y = tmp;
        }
        x[i] = point_x;
        y[i] = point_y;
        if (strength != nullptr) {
            strength[i] = 0;
        }

        LOG_I(TAG, "%d, %d", point_x, point_y);
    }

    *point_count = count;
    return count > 0;
}

static error_t axs5106_set_swap_xy(Device* device, bool swap) {
    auto* internal = static_cast<Axs5106Internal*>(device_get_driver_data(device));
    internal->swap_xy = swap;
    return ERROR_NONE;
}

static error_t axs5106_get_swap_xy(Device* device, bool* swap) {
    auto* internal = static_cast<Axs5106Internal*>(device_get_driver_data(device));
    *swap = internal->swap_xy;
    return ERROR_NONE;
}

static error_t axs5106_set_mirror_x(Device* device, bool mirror) {
    auto* internal = static_cast<Axs5106Internal*>(device_get_driver_data(device));
    internal->mirror_x = mirror;
    return ERROR_NONE;
}

static error_t axs5106_get_mirror_x(Device* device, bool* mirror) {
    auto* internal = static_cast<Axs5106Internal*>(device_get_driver_data(device));
    *mirror = internal->mirror_x;
    return ERROR_NONE;
}

static error_t axs5106_set_mirror_y(Device* device, bool mirror) {
    auto* internal = static_cast<Axs5106Internal*>(device_get_driver_data(device));
    internal->mirror_y = mirror;
    return ERROR_NONE;
}

static error_t axs5106_get_mirror_y(Device* device, bool* mirror) {
    auto* internal = static_cast<Axs5106Internal*>(device_get_driver_data(device));
    *mirror = internal->mirror_y;
    return ERROR_NONE;
}

// endregion

static const PointerApi axs5106_pointer_api = {
    // Unsupported (null per PointerApi's contract): neither the vendor demo nor the original
    // esp_lcd_touch component implements a sleep mode for this chip.
    .enter_sleep = nullptr,
    .exit_sleep = nullptr,
    .read_data = axs5106_read_data,
    .get_touched_points = axs5106_get_touched_points,
    .set_swap_xy = axs5106_set_swap_xy,
    .get_swap_xy = axs5106_get_swap_xy,
    .set_mirror_x = axs5106_set_mirror_x,
    .get_mirror_x = axs5106_get_mirror_x,
    .set_mirror_y = axs5106_set_mirror_y,
    .get_mirror_y = axs5106_get_mirror_y,
};

Driver axs5106_driver = {
    .name = "axs5106",
    .compatible = (const char*[]) { "axs,axs5106", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &axs5106_pointer_api,
    .device_type = &POINTER_TYPE,
    .owner = &axs5106_module,
    .internal = nullptr
};
