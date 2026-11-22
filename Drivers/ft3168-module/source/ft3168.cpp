// SPDX-License-Identifier: Apache-2.0
#include <drivers/ft3168.h>

#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/touch.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/Memory.hpp>

#include <utility>

static constexpr const char* TAG = "FT3168";

static const Ft3168Config* get_config(const Device* device) {
    return static_cast<const Ft3168Config*>(device->config);
}

static const TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

static constexpr uint8_t MAX_POINTS = 2;
static constexpr uint8_t REG_TOUCH_COUNT = 0x02;
static constexpr uint8_t REG_AUTO_MONITOR = 0x86;
static constexpr uint8_t REG_POWER_MODE = 0xA5;
static constexpr uint8_t POWER_MODE_ACTIVE = 0;
static constexpr uint8_t POWER_MODE_HIBERNATE = 3;
// Touch count, then 6 bytes per point (event/XH, XL, YH/id, YL, weight, area)
static constexpr uint8_t POINT_SIZE = 6;
static constexpr uint8_t FRAME_SIZE = 1 + MAX_POINTS * POINT_SIZE;

struct Ft3168Internal {
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    uint8_t point_count;
    uint16_t x[MAX_POINTS];
    uint16_t y[MAX_POINTS];
    uint16_t strength[MAX_POINTS];
};

static bool write_register(Device* i2c, uint8_t address, uint8_t reg, uint8_t value) {
    uint8_t data[2] = { reg, value };
    return i2c_controller_write(i2c, address, data, sizeof(data), I2C_TIMEOUT) == ERROR_NONE;
}

static error_t perform_reset(const Ft3168Config* config) {
    if (config->pin_reset.gpio_controller == nullptr) {
        return ERROR_NONE;
    }
    auto* rst = gpio_descriptor_acquire(config->pin_reset.gpio_controller, config->pin_reset.pin, GPIO_FLAG_DIRECTION_OUTPUT | GPIO_FLAG_ACTIVE_LOW, GPIO_OWNER_GPIO);
    if (rst == nullptr) {
        LOG_E(TAG, "Failed to acquire reset pin");
        return ERROR_RESOURCE;
    }
    error_t error = gpio_descriptor_set_level(rst, true);
    if (error == ERROR_NONE) {
        delay_millis(10);
        error = gpio_descriptor_set_level(rst, false);
    }
    gpio_descriptor_release(rst);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to pulse reset pin");
        return error;
    }
    delay_millis(50);
    return ERROR_NONE;
}

// The controller can fall into monitor mode, where it NAKs I2C reads until touched.
// Keep it active so periodic polling always gets an answer.
static error_t set_active_mode(Device* i2c, uint8_t address) {
    if (!write_register(i2c, address, REG_AUTO_MONITOR, 0) || !write_register(i2c, address, REG_POWER_MODE, POWER_MODE_ACTIVE)) {
        LOG_E(TAG, "Failed to set active power mode");
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = get_config(device);

    auto* internal = tt::newObject<Ft3168Internal>();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->swap_xy = config->swap_xy;
    internal->mirror_x = config->mirror_x;
    internal->mirror_y = config->mirror_y;

    error_t error = perform_reset(config);
    if (error == ERROR_NONE) {
        error = set_active_mode(parent, config->address);
    }
    if (error != ERROR_NONE) {
        tt::deleteObject(internal);
        return error;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    tt::deleteObject(static_cast<Ft3168Internal*>(device_get_driver_data(device)));
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

static error_t ft3168_enter_sleep(Device* device) {
    const auto* config = get_config(device);
    // Only a reset wakes the controller from hibernate
    if (config->pin_reset.gpio_controller == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    return write_register(device_get_parent(device), config->address, REG_POWER_MODE, POWER_MODE_HIBERNATE) ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t ft3168_exit_sleep(Device* device) {
    const auto* config = get_config(device);
    const error_t error = perform_reset(config);
    if (error != ERROR_NONE) {
        return error;
    }
    return set_active_mode(device_get_parent(device), config->address);
}

static error_t ft3168_read_data(Device* device, TickType_t timeout) {
    auto* internal = static_cast<Ft3168Internal*>(device_get_driver_data(device));
    const auto* config = get_config(device);

    uint8_t buffer[FRAME_SIZE];
    uint8_t reg = REG_TOUCH_COUNT;
    const error_t error = i2c_controller_write_read(device_get_parent(device), config->address, &reg, 1, buffer, sizeof(buffer), timeout);
    if (error != ERROR_NONE) {
        return error;
    }

    uint8_t count = buffer[0] & 0x0F;
    if (count > MAX_POINTS) {
        count = 0;
    }

    for (uint8_t i = 0; i < count; i++) {
        const uint8_t* point = &buffer[1 + i * POINT_SIZE];
        uint16_t x = static_cast<uint16_t>(((point[0] & 0x0F) << 8) | point[1]);
        uint16_t y = static_cast<uint16_t>(((point[2] & 0x0F) << 8) | point[3]);

        if (internal->swap_xy) {
            std::swap(x, y);
        }
        if (internal->mirror_x && config->x_max > 0) {
            x = static_cast<uint16_t>(config->x_max - 1 - x);
        }
        if (internal->mirror_y && config->y_max > 0) {
            y = static_cast<uint16_t>(config->y_max - 1 - y);
        }

        internal->x[i] = x;
        internal->y[i] = y;
        internal->strength[i] = point[4];
    }
    internal->point_count = count;
    return ERROR_NONE;
}

static bool ft3168_get_touched_points(Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    auto* internal = static_cast<Ft3168Internal*>(device_get_driver_data(device));
    uint8_t count = internal->point_count < max_point_count ? internal->point_count : max_point_count;
    for (uint8_t i = 0; i < count; i++) {
        x[i] = internal->x[i];
        y[i] = internal->y[i];
        if (strength != nullptr) {
            strength[i] = internal->strength[i];
        }
    }
    *point_count = count;
    return count > 0;
}

static error_t ft3168_set_swap_xy(Device* device, bool swap) {
    static_cast<Ft3168Internal*>(device_get_driver_data(device))->swap_xy = swap;
    return ERROR_NONE;
}

static error_t ft3168_get_swap_xy(Device* device, bool* swap) {
    *swap = static_cast<Ft3168Internal*>(device_get_driver_data(device))->swap_xy;
    return ERROR_NONE;
}

static error_t ft3168_set_mirror_x(Device* device, bool mirror) {
    static_cast<Ft3168Internal*>(device_get_driver_data(device))->mirror_x = mirror;
    return ERROR_NONE;
}

static error_t ft3168_get_mirror_x(Device* device, bool* mirror) {
    *mirror = static_cast<Ft3168Internal*>(device_get_driver_data(device))->mirror_x;
    return ERROR_NONE;
}

static error_t ft3168_set_mirror_y(Device* device, bool mirror) {
    static_cast<Ft3168Internal*>(device_get_driver_data(device))->mirror_y = mirror;
    return ERROR_NONE;
}

static error_t ft3168_get_mirror_y(Device* device, bool* mirror) {
    *mirror = static_cast<Ft3168Internal*>(device_get_driver_data(device))->mirror_y;
    return ERROR_NONE;
}

static const TouchApi ft3168_touch_api = {
    .enter_sleep = ft3168_enter_sleep,
    .exit_sleep = ft3168_exit_sleep,
    .read_data = ft3168_read_data,
    .get_touched_points = ft3168_get_touched_points,
    .set_swap_xy = ft3168_set_swap_xy,
    .get_swap_xy = ft3168_get_swap_xy,
    .set_mirror_x = ft3168_set_mirror_x,
    .get_mirror_x = ft3168_get_mirror_x,
    .set_mirror_y = ft3168_set_mirror_y,
    .get_mirror_y = ft3168_get_mirror_y,
};

Driver ft3168_driver = {
    .name = "ft3168",
    .compatible = (const char*[]) { "focaltech,ft3168", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &ft3168_touch_api,
    .device_type = &TOUCH_TYPE,
    .internal = nullptr
};
