// SPDX-License-Identifier: Apache-2.0
#include <drivers/pi4ioe5v6408.h>
#include <pi4ioe5v6408_module.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#define TAG "PI4IOE5V6408"

#define GET_CONFIG(device) (static_cast<const Pi4ioe5v6408Config*>((device)->config))

constexpr auto PI4_REGISTER_DIRECTION = 0x03;
constexpr auto PI4_REGISTER_OUTPUT_LEVEL = 0x05;
constexpr auto PI4_REGISTER_OUTPUT_HIGH_IMPEDANCE = 0x07;
constexpr auto PI4_REGISTER_INPUT_DEFAULT_LEVEL = 0x09;
constexpr auto PI4_REGISTER_PULL_ENABLE = 0x0B;
constexpr auto PI4_REGISTER_PULL_SELECT = 0x0D;
constexpr auto PI4_REGISTER_INPUT_LEVEL = 0x0F;
constexpr auto PI4_REGISTER_INTERRUPT_MASK = 0x11;
constexpr auto PI4_REGISTER_INTERRUPT_LEVEL = 0x13;

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    if (device_get_type(parent) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent device is not I2C");
        return ERROR_RESOURCE;
    }
    LOG_I(TAG, "Started PI4IOE5V6408 device %s", device->name);

    return gpio_controller_init_descriptors(device, 8, nullptr);
}

static error_t stop(Device* device) {
    check(gpio_controller_deinit_descriptors(device) == ERROR_NONE);
    return ERROR_NONE;
}

extern "C" {


static error_t set_level(GpioDescriptor* descriptor, bool high) {
    auto* device = descriptor->controller;
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t bit = 1 << descriptor->pin;

    if (high) {
        return i2c_controller_register8_set_bits(parent, address, PI4_REGISTER_OUTPUT_LEVEL, bit, portMAX_DELAY);
    } else {
        return i2c_controller_register8_reset_bits(parent, address, PI4_REGISTER_OUTPUT_LEVEL, bit, portMAX_DELAY);
    }
}

static error_t get_level(GpioDescriptor* descriptor, bool* high) {
    auto* device = descriptor->controller;
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t bits;

    error_t err = i2c_controller_register8_get(parent, address, PI4_REGISTER_INPUT_LEVEL, &bits, portMAX_DELAY);
    if (err != ERROR_NONE) {
        return err;
    }

    *high = (bits & (1 << descriptor->pin)) != 0;
    return ERROR_NONE;
}

static error_t set_flags(GpioDescriptor* descriptor, gpio_flags_t flags) {
    auto* device = descriptor->controller;
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t bit = 1 << descriptor->pin;

    error_t err;

    // Direction
    if (flags & GPIO_FLAG_DIRECTION_OUTPUT) {
        err = i2c_controller_register8_set_bits(parent, address, PI4_REGISTER_DIRECTION, bit, portMAX_DELAY);
    } else {
        err = i2c_controller_register8_reset_bits(parent, address, PI4_REGISTER_DIRECTION, bit, portMAX_DELAY);
    }

    if (err != ERROR_NONE) {
        return err;
    }

    // High Impedance
    if (flags & GPIO_FLAG_HIGH_IMPEDANCE) {
        err = i2c_controller_register8_set_bits(parent, address, PI4_REGISTER_OUTPUT_HIGH_IMPEDANCE, bit, portMAX_DELAY);
    } else {
        err = i2c_controller_register8_reset_bits(parent, address, PI4_REGISTER_OUTPUT_HIGH_IMPEDANCE, bit, portMAX_DELAY);
    }

    if (err != ERROR_NONE) {
        return err;
    }

    // Pull-up/down
    if (flags & (GPIO_FLAG_PULL_UP | GPIO_FLAG_PULL_DOWN)) {
        // Set pull up or pull down
        if (flags & GPIO_FLAG_PULL_UP) {
            err = i2c_controller_register8_set_bits(parent, address, PI4_REGISTER_PULL_SELECT, bit, portMAX_DELAY);
        } else {
            err = i2c_controller_register8_reset_bits(parent, address, PI4_REGISTER_PULL_SELECT, bit, portMAX_DELAY);
        }

        if (err != ERROR_NONE) {
            return err;
        }

        // Enable pull-up/down
        err = i2c_controller_register8_set_bits(parent, address, PI4_REGISTER_PULL_ENABLE, bit, portMAX_DELAY);
    } else {
        // Disable pull-up/down
        err = i2c_controller_register8_reset_bits(parent, address, PI4_REGISTER_PULL_ENABLE, bit, portMAX_DELAY);
    }

    return err;
}

static error_t get_flags(GpioDescriptor* descriptor, gpio_flags_t* flags) {
    auto* device = descriptor->controller;
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t bit = 1 << descriptor->pin;
    uint8_t val;
    error_t err;

    gpio_flags_t f = GPIO_FLAG_NONE;

    // Direction
    err = i2c_controller_register8_get(parent, address, PI4_REGISTER_DIRECTION, &val, portMAX_DELAY);
    if (err != ERROR_NONE) return err;
    if (val & bit) {
        f |= GPIO_FLAG_DIRECTION_OUTPUT;
    } else {
        f |= GPIO_FLAG_DIRECTION_INPUT;
    }

    // Pull-up/down
    err = i2c_controller_register8_get(parent, address, PI4_REGISTER_PULL_ENABLE, &val, portMAX_DELAY);
    if (err != ERROR_NONE) return err;
    if (val & bit) {
        err = i2c_controller_register8_get(parent, address, PI4_REGISTER_PULL_SELECT, &val, portMAX_DELAY);
        if (err != ERROR_NONE) return err;
        if (val & bit) {
            f |= GPIO_FLAG_PULL_UP;
        } else {
            f |= GPIO_FLAG_PULL_DOWN;
        }
    }

    // High Impedance
    err = i2c_controller_register8_get(parent, address, PI4_REGISTER_OUTPUT_HIGH_IMPEDANCE, &val, portMAX_DELAY);
    if (err != ERROR_NONE) return err;
    if (val & bit) {
        f |= GPIO_FLAG_HIGH_IMPEDANCE;
    }

    *flags = f;
    return ERROR_NONE;
}

static error_t get_native_pin_number(GpioDescriptor* descriptor, void* pin_number) {
    return ERROR_NOT_SUPPORTED;
}

static error_t add_callback(GpioDescriptor* descriptor, void (*callback)(void*), void* arg) {
    return ERROR_NOT_SUPPORTED;
}

static error_t remove_callback(GpioDescriptor* descriptor) {
    return ERROR_NOT_SUPPORTED;
}

static error_t enable_interrupt(GpioDescriptor* descriptor) {
    return ERROR_NOT_SUPPORTED;
}

static error_t disable_interrupt(GpioDescriptor* descriptor) {
    return ERROR_NOT_SUPPORTED;
}

const static GpioControllerApi pi4_gpio_api  = {
    .set_level = set_level,
    .get_level = get_level,
    .set_flags = set_flags,
    .get_flags = get_flags,
    .get_native_pin_number = get_native_pin_number,
    .add_callback = add_callback,
    .remove_callback = remove_callback,
    .enable_interrupt = enable_interrupt,
    .disable_interrupt = disable_interrupt
};

Driver pi4ioe5v6408_driver = {
    .name = "pi4ioe5v6408",
    .compatible = (const char*[]) { "diodes,pi4ioe5v6408", nullptr},
    .start_device = start,
    .stop_device = stop,
    .api = static_cast<const void*>(&pi4_gpio_api),
    .device_type = &GPIO_CONTROLLER_TYPE,
    .owner = &pi4ioe5v6408_module,
    .internal = nullptr
};

}