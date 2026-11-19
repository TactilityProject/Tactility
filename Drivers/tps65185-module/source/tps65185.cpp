// SPDX-License-Identifier: Apache-2.0
#include "tps65185_gpio.h"

#include <drivers/tps65185.h>

#include <tactility/device.h>
#include <tactility/delay.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#include <new>

#define TAG "Tps65185"

// Register map
// See: https://www.ti.com/lit/ds/symlink/tps65185.pdf
static constexpr uint8_t REG_ENABLE = 0x01;
static constexpr uint8_t REG_VCOM1 = 0x03;
static constexpr uint8_t REG_VCOM2 = 0x04;
static constexpr uint8_t REG_PG = 0x0F;

static constexpr uint8_t ENABLE_ALL_RAILS = 0x3F;

static constexpr uint8_t POWER_GOOD_MASK = 0xFA;

static constexpr uint32_t VCOM_STEP_MILLIVOLT = 10;
static constexpr uint32_t VCOM_MAX_MILLIVOLT = 511 * VCOM_STEP_MILLIVOLT;

static constexpr uint32_t I2C_TIMEOUT_MS = 100;
static constexpr uint32_t WAKEUP_SETTLE_MS = 3;
static constexpr uint32_t PIN_POWER_GOOD_TIMEOUT_MS = 500;
static constexpr uint32_t RAIL_POWER_GOOD_TIMEOUT_MS = 400;
static constexpr uint32_t POWER_GOOD_POLL_MS = 1;
static constexpr uint32_t DISCHARGE_MS = 1;

#define GET_CONFIG(device) (static_cast<const Tps65185Config*>((device)->config))
#define GET_INTERNAL(device) (static_cast<Tps65185Internal*>(device_get_driver_data(device)))

struct Tps65185Internal {
    Device* i2c = nullptr;
    uint8_t address = 0;
    uint32_t vcom_millivolt = 0;
    GpioDescriptor* wakeup = nullptr;
    GpioDescriptor* pwrup = nullptr;
    GpioDescriptor* vcom_ctrl = nullptr;
    GpioDescriptor* power_good = nullptr;
    bool on = false;
};

static void release(GpioDescriptor** descriptor) {
    if (*descriptor != nullptr) {
        gpio_descriptor_release(*descriptor);
        *descriptor = nullptr;
    }
}

static error_t wait_for_power_good_pin(Tps65185Internal* internal) {
    for (uint32_t waited = 0; waited < PIN_POWER_GOOD_TIMEOUT_MS; waited += POWER_GOOD_POLL_MS) {
        bool good = false;
        error_t error = gpio_descriptor_get_level(internal->power_good, &good);
        if (error != ERROR_NONE) return error;
        if (good) return ERROR_NONE;
        delay_millis(POWER_GOOD_POLL_MS);
    }
    return ERROR_TIMEOUT;
}

static error_t wait_for_rails(Tps65185Internal* internal) {
    const TickType_t timeout = millis_to_ticks(I2C_TIMEOUT_MS);
    for (uint32_t waited = 0; waited < RAIL_POWER_GOOD_TIMEOUT_MS; waited += POWER_GOOD_POLL_MS) {
        uint8_t pg = 0;
        error_t error = i2c_controller_register8_get(internal->i2c, internal->address, REG_PG, &pg, timeout);
        if (error != ERROR_NONE) return error;
        if ((pg & POWER_GOOD_MASK) == POWER_GOOD_MASK) return ERROR_NONE;
        delay_millis(POWER_GOOD_POLL_MS);
    }
    return ERROR_TIMEOUT;
}

static error_t write_vcom(Tps65185Internal* internal) {
    const uint32_t steps = internal->vcom_millivolt / VCOM_STEP_MILLIVOLT;
    const TickType_t timeout = millis_to_ticks(I2C_TIMEOUT_MS);
    error_t error = i2c_controller_register8_set(internal->i2c, internal->address, REG_VCOM1, static_cast<uint8_t>(steps & 0xFF), timeout);
    if (error != ERROR_NONE) return error;
    return i2c_controller_register8_set(internal->i2c, internal->address, REG_VCOM2, static_cast<uint8_t>((steps >> 8) & 0x01), timeout);
}

static void set_pin(GpioDescriptor* descriptor, bool level) {
    error_t error = gpio_descriptor_set_level(descriptor, level);
    if (error != ERROR_NONE) {
        LOG_W(TAG, "Failed to set a supply control pin: %d", error);
    }
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);
    auto* i2c = device_get_parent(device);
    if (device_get_type(i2c) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }
    if (config->vcom_millivolt > VCOM_MAX_MILLIVOLT) {
        LOG_E(TAG, "VCOM %u mV exceeds the TPS65185 range", config->vcom_millivolt);
        return ERROR_INVALID_ARGUMENT;
    }

    auto* internal = new (std::nothrow) Tps65185Internal();
    if (internal == nullptr) return ERROR_OUT_OF_MEMORY;

    internal->i2c = i2c;
    internal->address = config->address;
    internal->vcom_millivolt = config->vcom_millivolt;

    error_t error = tps65185_gpio_acquire_output(config->pin_wakeup, false, &internal->wakeup);
    if (error == ERROR_NONE) error = tps65185_gpio_acquire_output(config->pin_pwrup, false, &internal->pwrup);
    if (error == ERROR_NONE) error = tps65185_gpio_acquire_output(config->pin_vcom_ctrl, false, &internal->vcom_ctrl);
    if (error == ERROR_NONE) error = tps65185_gpio_acquire_input(config->pin_power_good, &internal->power_good);
    if (error != ERROR_NONE) {
        release(&internal->wakeup);
        release(&internal->pwrup);
        release(&internal->vcom_ctrl);
        release(&internal->power_good);
        delete internal;
        return error;
    }

    device_set_driver_data(device, internal);
    error = tps65185_on(device);
    if (error != ERROR_NONE) {
        device_set_driver_data(device, nullptr);
        release(&internal->wakeup);
        release(&internal->pwrup);
        release(&internal->vcom_ctrl);
        release(&internal->power_good);
        delete internal;
        return error;
    }
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);
    tps65185_off(device);

    release(&internal->wakeup);
    release(&internal->pwrup);
    release(&internal->vcom_ctrl);
    release(&internal->power_good);

    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

extern "C" {

error_t tps65185_on(Device* device) {
    auto* internal = GET_INTERNAL(device);
    if (internal == nullptr) return ERROR_INVALID_STATE;
    if (internal->on) return ERROR_NONE;

    set_pin(internal->wakeup, true);
    set_pin(internal->pwrup, true);
    set_pin(internal->vcom_ctrl, true);
    delay_millis(WAKEUP_SETTLE_MS);

    error_t error = wait_for_power_good_pin(internal);
    if (error == ERROR_NONE) {
        error = i2c_controller_register8_set(internal->i2c, internal->address, REG_ENABLE, ENABLE_ALL_RAILS, millis_to_ticks(I2C_TIMEOUT_MS));
    }
    if (error == ERROR_NONE) error = write_vcom(internal);
    if (error == ERROR_NONE) error = wait_for_rails(internal);

    internal->on = true;
    if (error != ERROR_NONE) {
        LOG_E(TAG, "TPS65185 rails did not come up: %d", error);
        tps65185_off(device);
        return error;
    }
    return ERROR_NONE;
}

void tps65185_off(Device* device) {
    auto* internal = GET_INTERNAL(device);
    if (internal == nullptr || !internal->on) return;

    set_pin(internal->pwrup, false);
    set_pin(internal->vcom_ctrl, false);
    delay_millis(DISCHARGE_MS);
    set_pin(internal->wakeup, false);
    internal->on = false;
}

error_t tps65185_set_vcom(Device* device, uint32_t millivolt) {
    auto* internal = GET_INTERNAL(device);
    if (internal == nullptr || millivolt > VCOM_MAX_MILLIVOLT) return ERROR_INVALID_ARGUMENT;

    internal->vcom_millivolt = millivolt;
    if (!internal->on) return ERROR_NONE;
    return write_vcom(internal);
}

Driver tps65185_driver = {
    .name = "tps65185",
    .compatible = (const char*[]) { "ti,tps65185", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = nullptr,
    .device_type = nullptr,
    .internal = nullptr
};

} // extern "C"
