// SPDX-License-Identifier: Apache-2.0
#include <drivers/tca9534.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#define TAG "TCA9534"

#define GET_CONFIG(device) (static_cast<const Tca9534Config*>((device)->config))

constexpr auto TCA9534_REGISTER_INPUT = 0x00;
constexpr auto TCA9534_REGISTER_OUTPUT = 0x01;
constexpr auto TCA9534_REGISTER_POLARITY = 0x02;
constexpr auto TCA9534_REGISTER_CONFIG = 0x03;

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    if (device_get_type(parent) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent device is not I2C");
        return ERROR_RESOURCE;
    }

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
    // The output port has no polarity inversion, so active-low outputs are inverted here
    bool physical_high = (descriptor->flags & GPIO_FLAG_ACTIVE_LOW) != 0 ? !high : high;

    // i2c_controller_register8_{set,reset}_bits() do a separate read then
    // write; without this lock, concurrent updates to different pins on the
    // same output register can clobber each other.
    device_lock(device);
    error_t err = physical_high
        ? i2c_controller_register8_set_bits(parent, address, TCA9534_REGISTER_OUTPUT, bit, portMAX_DELAY)
        : i2c_controller_register8_reset_bits(parent, address, TCA9534_REGISTER_OUTPUT, bit, portMAX_DELAY);
    device_unlock(device);
    return err;
}

static error_t get_level(GpioDescriptor* descriptor, bool* high) {
    auto* device = descriptor->controller;
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t bits;

    error_t err = i2c_controller_register8_get(parent, address, TCA9534_REGISTER_INPUT, &bits, portMAX_DELAY);
    if (err != ERROR_NONE) {
        return err;
    }

    *high = (bits & (1 << descriptor->pin)) != 0;
    return ERROR_NONE;
}

static error_t set_flags(GpioDescriptor* descriptor, gpio_flags_t flags) {
    // The TCA9534 only supports direction and polarity inversion. Pull-up/down and
    // high-impedance are not present in its register map.
    if (flags & (GPIO_FLAG_PULL_UP | GPIO_FLAG_PULL_DOWN | GPIO_FLAG_HIGH_IMPEDANCE)) {
        return ERROR_NOT_SUPPORTED;
    }

    auto* device = descriptor->controller;
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    uint8_t bit = 1 << descriptor->pin;
    error_t err;

    // Locked as a whole: direction and polarity are two separate read-modify-write
    // register updates, and both should apply atomically with respect to other
    // set_flags / set_level calls on this device.
    device_lock(device);

    // Direction: configuration bit is 1 for input, 0 for output.
    if (flags & GPIO_FLAG_DIRECTION_OUTPUT) {
        err = i2c_controller_register8_reset_bits(parent, address, TCA9534_REGISTER_CONFIG, bit, portMAX_DELAY);
    } else {
        err = i2c_controller_register8_set_bits(parent, address, TCA9534_REGISTER_CONFIG, bit, portMAX_DELAY);
    }

    if (err != ERROR_NONE) {
        device_unlock(device);
        return err;
    }

    // Input polarity inversion, so get_level() reports the logical level of active-low pins
    if (flags & GPIO_FLAG_ACTIVE_LOW) {
        err = i2c_controller_register8_set_bits(parent, address, TCA9534_REGISTER_POLARITY, bit, portMAX_DELAY);
    } else {
        err = i2c_controller_register8_reset_bits(parent, address, TCA9534_REGISTER_POLARITY, bit, portMAX_DELAY);
    }

    device_unlock(device);
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

    err = i2c_controller_register8_get(parent, address, TCA9534_REGISTER_CONFIG, &val, portMAX_DELAY);
    if (err != ERROR_NONE) return err;
    f |= (val & bit) ? GPIO_FLAG_DIRECTION_INPUT : GPIO_FLAG_DIRECTION_OUTPUT;

    err = i2c_controller_register8_get(parent, address, TCA9534_REGISTER_POLARITY, &val, portMAX_DELAY);
    if (err != ERROR_NONE) return err;
    f |= (val & bit) ? GPIO_FLAG_ACTIVE_LOW : GPIO_FLAG_ACTIVE_HIGH;

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

const static GpioControllerApi tca9534_gpio_api = {
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

Driver tca9534_driver = {
    .name = "tca9534",
    .compatible = (const char*[]) { "ti,tca9534", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = static_cast<const void*>(&tca9534_gpio_api),
    .device_type = &GPIO_CONTROLLER_TYPE,
    .internal = nullptr
};

}
