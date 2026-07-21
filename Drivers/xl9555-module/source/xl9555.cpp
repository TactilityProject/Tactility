// SPDX-License-Identifier: Apache-2.0
#include <drivers/xl9555.h>
#include <xl9555_module.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#define TAG "XL9555"

#define GET_CONFIG(device) (static_cast<const Xl9555Config*>((device)->config))

// PCA9555-compatible register map: one register per function per 8-pin port.
constexpr auto XL9555_REGISTER_INPUT_PORT0 = 0x00;
constexpr auto XL9555_REGISTER_OUTPUT_PORT0 = 0x02;
constexpr auto XL9555_REGISTER_POLARITY_PORT0 = 0x04;
constexpr auto XL9555_REGISTER_CONFIG_PORT0 = 0x06;

static inline uint8_t port_of(GpioDescriptor* descriptor) {
    return descriptor->pin >> 3;
}

static inline uint8_t bit_of(GpioDescriptor* descriptor) {
    return 1 << (descriptor->pin & 0x7);
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    return gpio_controller_init_descriptors(device, 16, nullptr);
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
    auto reg = static_cast<uint8_t>(XL9555_REGISTER_OUTPUT_PORT0 + port_of(descriptor));
    auto bit = bit_of(descriptor);

    // i2c_controller_register8_{set,reset}_bits() do a separate read then
    // write; without this lock, concurrent updates to different pins on the
    // same output port register can clobber each other.
    device_lock(device);
    error_t err = high
        ? i2c_controller_register8_set_bits(parent, address, reg, bit, portMAX_DELAY)
        : i2c_controller_register8_reset_bits(parent, address, reg, bit, portMAX_DELAY);
    device_unlock(device);
    return err;
}

static error_t get_level(GpioDescriptor* descriptor, bool* high) {
    auto* device = descriptor->controller;
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    auto reg = static_cast<uint8_t>(XL9555_REGISTER_INPUT_PORT0 + port_of(descriptor));
    uint8_t bits;

    error_t err = i2c_controller_register8_get(parent, address, reg, &bits, portMAX_DELAY);
    if (err != ERROR_NONE) {
        return err;
    }

    *high = (bits & bit_of(descriptor)) != 0;
    return ERROR_NONE;
}

static error_t set_flags(GpioDescriptor* descriptor, gpio_flags_t flags) {
    // The XL9555 only supports direction and polarity inversion. Pull-up/down and
    // high-impedance are not present in its PCA9555-compatible register map.
    if (flags & (GPIO_FLAG_PULL_UP | GPIO_FLAG_PULL_DOWN | GPIO_FLAG_HIGH_IMPEDANCE)) {
        return ERROR_NOT_SUPPORTED;
    }

    // The polarity register only inverts what's read back from an input pin;
    // set_level() still drives outputs at the raw level. Accepting ACTIVE_LOW
    // on an output would silently not do what it implies.
    if ((flags & GPIO_FLAG_ACTIVE_LOW) && (flags & GPIO_FLAG_DIRECTION_OUTPUT)) {
        return ERROR_NOT_SUPPORTED;
    }

    auto* device = descriptor->controller;
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    auto config_reg = static_cast<uint8_t>(XL9555_REGISTER_CONFIG_PORT0 + port_of(descriptor));
    auto polarity_reg = static_cast<uint8_t>(XL9555_REGISTER_POLARITY_PORT0 + port_of(descriptor));
    auto bit = bit_of(descriptor);
    error_t err;

    // Locked as a whole: direction and polarity are two separate RMW register
    // writes, and both should apply atomically with respect to other set_flags
    // / set_level calls on this device.
    device_lock(device);

    // Direction: configuration bit is 1 for input, 0 for output.
    if (flags & GPIO_FLAG_DIRECTION_OUTPUT) {
        err = i2c_controller_register8_reset_bits(parent, address, config_reg, bit, portMAX_DELAY);
    } else {
        err = i2c_controller_register8_set_bits(parent, address, config_reg, bit, portMAX_DELAY);
    }

    if (err != ERROR_NONE) {
        device_unlock(device);
        return err;
    }

    // Polarity inversion (mainly relevant for active-low inputs).
    if (flags & GPIO_FLAG_ACTIVE_LOW) {
        err = i2c_controller_register8_set_bits(parent, address, polarity_reg, bit, portMAX_DELAY);
    } else {
        err = i2c_controller_register8_reset_bits(parent, address, polarity_reg, bit, portMAX_DELAY);
    }

    device_unlock(device);
    return err;
}

static error_t get_flags(GpioDescriptor* descriptor, gpio_flags_t* flags) {
    auto* device = descriptor->controller;
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    auto config_reg = static_cast<uint8_t>(XL9555_REGISTER_CONFIG_PORT0 + port_of(descriptor));
    auto polarity_reg = static_cast<uint8_t>(XL9555_REGISTER_POLARITY_PORT0 + port_of(descriptor));
    auto bit = bit_of(descriptor);
    uint8_t val;
    error_t err;

    gpio_flags_t f = GPIO_FLAG_NONE;

    err = i2c_controller_register8_get(parent, address, config_reg, &val, portMAX_DELAY);
    if (err != ERROR_NONE) return err;
    f |= (val & bit) ? GPIO_FLAG_DIRECTION_INPUT : GPIO_FLAG_DIRECTION_OUTPUT;

    err = i2c_controller_register8_get(parent, address, polarity_reg, &val, portMAX_DELAY);
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

const static GpioControllerApi xl9555_gpio_api = {
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

Driver xl9555_driver = {
    .name = "xl9555",
    .compatible = (const char*[]) { "xlsemi,xl9555", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = static_cast<const void*>(&xl9555_gpio_api),
    .device_type = &GPIO_CONTROLLER_TYPE,
    .owner = &xl9555_module,
    .internal = nullptr
};

}
