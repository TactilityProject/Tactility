// SPDX-License-Identifier: Apache-2.0
#include <drivers/aw9523b.h>
#include <aw9523b_module.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#define TAG "AW9523B"

#define GET_CONFIG(device) (static_cast<const Aw9523bConfig*>((device)->config))

namespace {

// Port 0: pins 0..7, Port 1: pins 8..15.
constexpr auto AW9523B_REG_INPUT_P0 = 0x00;
constexpr auto AW9523B_REG_INPUT_P1 = 0x01;
constexpr auto AW9523B_REG_OUTPUT_P0 = 0x02;
constexpr auto AW9523B_REG_OUTPUT_P1 = 0x03;
// Configuration register: 0 = output, 1 = input (inverted vs. most other expanders).
constexpr auto AW9523B_REG_CONFIG_P0 = 0x04;
constexpr auto AW9523B_REG_CONFIG_P1 = 0x05;
// Global Control Register: bit 4 selects P0 drive mode (0 = open-drain default, 1 = push-pull).
constexpr auto AW9523B_REG_GCR = 0x11;
constexpr uint8_t AW9523B_GCR_P0_PUSH_PULL = 1U << 4U;

constexpr uint8_t portRegister(bool isPort1, uint8_t regPort0, uint8_t regPort1) {
    return isPort1 ? regPort1 : regPort0;
}

} // namespace

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    if (device_get_type(parent) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent device is not I2C");
        return ERROR_RESOURCE;
    }

    auto address = GET_CONFIG(device)->address;

    // P0 defaults to open-drain out of reset, which can't drive an active-high output
    // without an external pull-up. Every known board wiring for this chip expects
    // push-pull, so switch it unconditionally on start.
    if (i2c_controller_register8_set_bits(parent, address, AW9523B_REG_GCR, AW9523B_GCR_P0_PUSH_PULL, portMAX_DELAY) != ERROR_NONE) {
        LOG_E(TAG, "Failed to set P0 to push-pull mode");
        return ERROR_RESOURCE;
    }

    LOG_I(TAG, "Started AW9523B device %s", device->name);
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
    bool isPort1 = descriptor->pin >= 8;
    uint8_t bit = 1U << (descriptor->pin % 8U);
    uint8_t reg = portRegister(isPort1, AW9523B_REG_OUTPUT_P0, AW9523B_REG_OUTPUT_P1);

    // This chip has no output polarity register: active-low must be inverted in software.
    bool physical_high = (descriptor->flags & GPIO_FLAG_ACTIVE_LOW) != 0 ? !high : high;

    if (physical_high) {
        return i2c_controller_register8_set_bits(parent, address, reg, bit, portMAX_DELAY);
    } else {
        return i2c_controller_register8_reset_bits(parent, address, reg, bit, portMAX_DELAY);
    }
}

static error_t get_level(GpioDescriptor* descriptor, bool* high) {
    auto* device = descriptor->controller;
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    bool isPort1 = descriptor->pin >= 8;
    uint8_t bit = 1U << (descriptor->pin % 8U);
    uint8_t reg = portRegister(isPort1, AW9523B_REG_INPUT_P0, AW9523B_REG_INPUT_P1);

    uint8_t bits;
    error_t err = i2c_controller_register8_get(parent, address, reg, &bits, portMAX_DELAY);
    if (err != ERROR_NONE) {
        return err;
    }

    bool physical_high = (bits & bit) != 0;
    *high = (descriptor->flags & GPIO_FLAG_ACTIVE_LOW) != 0 ? !physical_high : physical_high;
    return ERROR_NONE;
}

static error_t set_flags(GpioDescriptor* descriptor, gpio_flags_t flags) {
    auto* device = descriptor->controller;
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    bool isPort1 = descriptor->pin >= 8;
    uint8_t bit = 1U << (descriptor->pin % 8U);
    uint8_t reg = portRegister(isPort1, AW9523B_REG_CONFIG_P0, AW9523B_REG_CONFIG_P1);

    // Config register is inverted: clear the bit for output, set it for input.
    if (flags & GPIO_FLAG_DIRECTION_OUTPUT) {
        return i2c_controller_register8_reset_bits(parent, address, reg, bit, portMAX_DELAY);
    } else {
        return i2c_controller_register8_set_bits(parent, address, reg, bit, portMAX_DELAY);
    }
}

static error_t get_flags(GpioDescriptor* descriptor, gpio_flags_t* flags) {
    auto* device = descriptor->controller;
    auto* parent = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;
    bool isPort1 = descriptor->pin >= 8;
    uint8_t bit = 1U << (descriptor->pin % 8U);
    uint8_t reg = portRegister(isPort1, AW9523B_REG_CONFIG_P0, AW9523B_REG_CONFIG_P1);

    uint8_t val;
    error_t err = i2c_controller_register8_get(parent, address, reg, &val, portMAX_DELAY);
    if (err != ERROR_NONE) {
        return err;
    }

    // Config register is inverted: 0 = output, 1 = input.
    *flags = (val & bit) ? GPIO_FLAG_DIRECTION_INPUT : GPIO_FLAG_DIRECTION_OUTPUT;
    *flags |= (descriptor->flags & GPIO_FLAG_ACTIVE_LOW);
    return ERROR_NONE;
}

static error_t get_native_pin_number(GpioDescriptor* descriptor, void* pin_number) {
    (void) descriptor;
    (void) pin_number;
    return ERROR_NOT_SUPPORTED;
}

static error_t add_callback(GpioDescriptor* descriptor, void (*callback)(void*), void* arg) {
    (void) descriptor;
    (void) callback;
    (void) arg;
    return ERROR_NOT_SUPPORTED;
}

static error_t remove_callback(GpioDescriptor* descriptor) {
    (void) descriptor;
    return ERROR_NOT_SUPPORTED;
}

static error_t enable_interrupt(GpioDescriptor* descriptor) {
    (void) descriptor;
    return ERROR_NOT_SUPPORTED;
}

static error_t disable_interrupt(GpioDescriptor* descriptor) {
    (void) descriptor;
    return ERROR_NOT_SUPPORTED;
}

const static GpioControllerApi aw9523b_gpio_api = {
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

Driver aw9523b_driver = {
    .name = "aw9523b",
    .compatible = (const char*[]) { "awinic,aw9523b", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = static_cast<const void*>(&aw9523b_gpio_api),
    .device_type = &GPIO_CONTROLLER_TYPE,
    .owner = &aw9523b_module,
    .internal = nullptr
};

}
