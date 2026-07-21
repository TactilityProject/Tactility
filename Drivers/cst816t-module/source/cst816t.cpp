// SPDX-License-Identifier: Apache-2.0
#include <drivers/cst816t.h>

#include <cst816t_module.h>

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

constexpr auto* TAG = "CST816T";
#define GET_CONFIG(device) (static_cast<const Cst816tConfig*>((device)->config))

// Register map and reset/bring-up sequence taken from https://github.com/koendv/cst816t
// (src/cst816t.cpp) - the manufacturer datasheet (DS-CST816T V2.2) covers electrical
// characteristics and I2C timing only, not the register map.
static constexpr uint8_t REG_GESTURE_ID = 0x01;
static constexpr uint8_t REG_CHIP_ID = 0xA7;
static constexpr uint8_t REG_DIS_AUTOSLEEP = 0xFE;

static constexpr TickType_t TIMEOUT = pdMS_TO_TICKS(50);

struct Cst816tInternal {
    GpioDescriptor* reset_descriptor;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    bool touched;
    uint16_t x;
    uint16_t y;
};

// region Driver lifecycle

// Timings match the reference driver linked above: it holds RST high for 100ms (well past the
// datasheet's Tpor/Tron minimum of 100ms), pulses it low for 10ms (Trst minimum is 0.1ms), then
// waits another 100ms (Tron) for the chip to finish reinitializing before any I2C traffic.
static error_t reset_pulse(GpioDescriptor* descriptor) {
    bool ok = gpio_descriptor_set_level(descriptor, false) == ERROR_NONE;
    vTaskDelay(pdMS_TO_TICKS(100));
    ok = ok && gpio_descriptor_set_level(descriptor, true) == ERROR_NONE;
    vTaskDelay(pdMS_TO_TICKS(10));
    ok = ok && gpio_descriptor_set_level(descriptor, false) == ERROR_NONE;
    vTaskDelay(pdMS_TO_TICKS(100));

    return ok ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Cst816tInternal*>(malloc(sizeof(Cst816tInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->reset_descriptor = nullptr;
    internal->swap_xy = config->swap_xy;
    internal->mirror_x = config->mirror_x;
    internal->mirror_y = config->mirror_y;
    internal->touched = false;
    internal->x = 0;
    internal->y = 0;

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

    uint8_t chip_id = 0;
    if (i2c_controller_register8_get(parent, config->address, REG_CHIP_ID, &chip_id, TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to read chip ID");
        if (internal->reset_descriptor != nullptr) {
            gpio_descriptor_release(internal->reset_descriptor);
        }
        free(internal);
        return ERROR_RESOURCE;
    }
    LOG_I(TAG, "Chip ID 0x%02X", chip_id);

    // Auto-sleep would otherwise drop the controller into standby after ~2s of inactivity,
    // adding wake-up latency to the next touch; disabled since exit_sleep() isn't implemented
    // here (see README) so there'd be no way back out once it kicked in.
    uint8_t dis_auto_sleep = 0xFF;
    if (i2c_controller_register8_set(parent, config->address, REG_DIS_AUTOSLEEP, dis_auto_sleep, TIMEOUT) != ERROR_NONE) {
        LOG_W(TAG, "Failed to disable auto-sleep (non-fatal)");
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Cst816tInternal*>(device_get_driver_data(device));

    if (internal->reset_descriptor != nullptr) {
        gpio_descriptor_release(internal->reset_descriptor);
    }

    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region PointerApi

static error_t cst816t_read_data(Device* device, TickType_t) {
    auto* internal = static_cast<Cst816tInternal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);
    auto* parent = device_get_parent(device);

    // Burst-reads gesture_id, finger_num and X/Y in one transaction, matching the reference
    // driver: the chip presents them as one contiguous 6-byte block starting at REG_GESTURE_ID.
    uint8_t data[6];
    error_t error = i2c_controller_read_register(parent, config->address, REG_GESTURE_ID, data, sizeof(data), TIMEOUT);
    if (error != ERROR_NONE) {
        return error;
    }

    uint8_t finger_num = data[1];
    internal->touched = finger_num > 0;
    // Top nibble of the high byte is reserved/event-type; the coordinate itself is 12-bit.
    internal->x = static_cast<uint16_t>(((data[2] & 0x0F) << 8) | data[3]);
    internal->y = static_cast<uint16_t>(((data[4] & 0x0F) << 8) | data[5]);

    return ERROR_NONE;
}

// Single-touch only - see README for why.
static bool cst816t_get_touched_points(Device* device, uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* point_count, uint8_t max_point_count) {
    auto* internal = static_cast<Cst816tInternal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);

    if (!internal->touched || max_point_count == 0) {
        *point_count = 0;
        return false;
    }

    // Order matches esp_lcd_touch's own software coordinate adjustment (mirror X, mirror Y,
    // then swap) so behavior lines up with the other touch modules in this codebase.
    uint16_t point_x = internal->x;
    uint16_t point_y = internal->y;
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

    x[0] = point_x;
    y[0] = point_y;
    if (strength != nullptr) {
        strength[0] = 0;
    }
    *point_count = 1;
    return true;
}

static error_t cst816t_set_swap_xy(Device* device, bool swap) {
    auto* internal = static_cast<Cst816tInternal*>(device_get_driver_data(device));
    internal->swap_xy = swap;
    return ERROR_NONE;
}

static error_t cst816t_get_swap_xy(Device* device, bool* swap) {
    auto* internal = static_cast<Cst816tInternal*>(device_get_driver_data(device));
    *swap = internal->swap_xy;
    return ERROR_NONE;
}

static error_t cst816t_set_mirror_x(Device* device, bool mirror) {
    auto* internal = static_cast<Cst816tInternal*>(device_get_driver_data(device));
    internal->mirror_x = mirror;
    return ERROR_NONE;
}

static error_t cst816t_get_mirror_x(Device* device, bool* mirror) {
    auto* internal = static_cast<Cst816tInternal*>(device_get_driver_data(device));
    *mirror = internal->mirror_x;
    return ERROR_NONE;
}

static error_t cst816t_set_mirror_y(Device* device, bool mirror) {
    auto* internal = static_cast<Cst816tInternal*>(device_get_driver_data(device));
    internal->mirror_y = mirror;
    return ERROR_NONE;
}

static error_t cst816t_get_mirror_y(Device* device, bool* mirror) {
    auto* internal = static_cast<Cst816tInternal*>(device_get_driver_data(device));
    *mirror = internal->mirror_y;
    return ERROR_NONE;
}

// endregion

static const PointerApi cst816t_pointer_api = {
    // Unsupported (null per PointerApi's contract): the reference driver defines
    // REG_SLEEP_MODE (0xE5) but never writes it, and the datasheet says waking up needs a
    // hardware reset pulse rather than a register write.
    .enter_sleep = nullptr,
    .exit_sleep = nullptr,
    .read_data = cst816t_read_data,
    .get_touched_points = cst816t_get_touched_points,
    .set_swap_xy = cst816t_set_swap_xy,
    .get_swap_xy = cst816t_get_swap_xy,
    .set_mirror_x = cst816t_set_mirror_x,
    .get_mirror_x = cst816t_get_mirror_x,
    .set_mirror_y = cst816t_set_mirror_y,
    .get_mirror_y = cst816t_get_mirror_y,
};

Driver cst816t_driver = {
    .name = "cst816t",
    .compatible = (const char*[]) { "hynitron,cst816t", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &cst816t_pointer_api,
    .device_type = &POINTER_TYPE,
    .owner = &cst816t_module,
    .internal = nullptr
};
