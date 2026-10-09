// SPDX-License-Identifier: Apache-2.0
#include <drivers/lp5814.h>
#include <lp5814_module.h>

#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#include <new>

constexpr auto* TAG = "LP5814";
#define GET_CONFIG(device) (static_cast<const Lp5814Config*>((device)->config))
#define GET_INTERNAL(device) (static_cast<Lp5814Internal*>(device_get_driver_data(device)))

static const TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

static constexpr uint8_t REG_CHIP_EN = 0x00;
static constexpr uint8_t REG_DEV_CONFIG0 = 0x01;
static constexpr uint8_t REG_DEV_CONFIG1 = 0x02;
static constexpr uint8_t REG_RESET_CMD = 0x0E;
static constexpr uint8_t REG_UPDATE_CMD = 0x0F;
static constexpr uint8_t REG_OUT0_DC = 0x14;
static constexpr uint8_t REG_OUT0_MANUAL_PWM = 0x18;

static constexpr uint8_t RESET_KEY = 0xCC;
static constexpr uint8_t UPDATE_KEY = 0x55;
static constexpr uint8_t CHANNEL_COUNT = 4;

extern "C" {

struct Lp5814Internal {
    uint8_t brightness;
};

static error_t write_register(Device* device, uint8_t reg, uint8_t value) {
    return i2c_controller_register8_set(device_get_parent(device), GET_CONFIG(device)->address, reg, value, I2C_TIMEOUT);
}

static error_t write_per_channel(Device* device, uint8_t first_reg, uint8_t value) {
    const auto* config = GET_CONFIG(device);
    uint8_t values[CHANNEL_COUNT];
    for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
        values[i] = (config->channels & (1U << i)) != 0 ? value : 0;
    }
    return i2c_controller_write_register(device_get_parent(device), config->address, first_reg, values, CHANNEL_COUNT, I2C_TIMEOUT);
}

// Registers 0x01 to 0x05 only take effect after the update command
static error_t configure(Device* device) {
    const auto* config = GET_CONFIG(device);

    error_t error = write_register(device, REG_RESET_CMD, RESET_KEY);
    if (error == ERROR_NONE) {
        delay_millis(1);
        error = write_register(device, REG_CHIP_EN, 0x01);
    }
    if (error == ERROR_NONE) {
        error = write_register(device, REG_DEV_CONFIG0, config->high_current ? 0x01 : 0x00);
    }
    if (error == ERROR_NONE) {
        error = write_register(device, REG_DEV_CONFIG1, config->channels & 0x0F);
    }
    if (error == ERROR_NONE) {
        error = write_register(device, REG_UPDATE_CMD, UPDATE_KEY);
    }
    if (error == ERROR_NONE) {
        error = write_per_channel(device, REG_OUT0_DC, config->dot_current);
    }
    return error;
}

// region BacklightApi

static error_t lp5814_set_brightness(Device* device, uint8_t brightness) {
    error_t error = write_per_channel(device, REG_OUT0_MANUAL_PWM, brightness);
    if (error != ERROR_NONE) {
        return error;
    }
    GET_INTERNAL(device)->brightness = brightness;
    return ERROR_NONE;
}

static error_t lp5814_set_brightness_default(Device* device) {
    return lp5814_set_brightness(device, GET_CONFIG(device)->brightness_default);
}

static error_t lp5814_get_brightness(Device* device, uint8_t* out_brightness) {
    *out_brightness = GET_INTERNAL(device)->brightness;
    return ERROR_NONE;
}

// endregion

static constexpr BacklightApi LP5814_BACKLIGHT_API = {
    .set_brightness = lp5814_set_brightness,
    .set_brightness_default = lp5814_set_brightness_default,
    .get_brightness = lp5814_get_brightness,
    .get_min_brightness = nullptr,
    .get_max_brightness = nullptr,
};

// region Driver lifecycle

static error_t start(Device* device) {
    check(device_get_type(device_get_parent(device)) == &I2C_CONTROLLER_TYPE);

    auto* internal = new(std::nothrow) Lp5814Internal { .brightness = 0 };
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    device_set_driver_data(device, internal);

    error_t error = configure(device);
    if (error == ERROR_NONE) {
        error = lp5814_set_brightness_default(device);
    }
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure");
        device_set_driver_data(device, nullptr);
        delete internal;
        return error;
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    // Allowed to fail, the device is stopped either way
    lp5814_set_brightness(device, 0);
    write_register(device, REG_CHIP_EN, 0x00);

    auto* internal = GET_INTERNAL(device);
    device_set_driver_data(device, nullptr);
    delete internal;

    return ERROR_NONE;
}

// endregion

Driver lp5814_driver = {
    .name = "lp5814",
    .compatible = (const char*[]) { "ti,lp5814", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &LP5814_BACKLIGHT_API,
    .device_type = &BACKLIGHT_TYPE,
    .owner = &lp5814_module,
    .internal = nullptr,
};

}
