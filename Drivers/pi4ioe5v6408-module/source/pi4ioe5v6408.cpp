// SPDX-License-Identifier: Apache-2.0
#include <drivers/pi4ioe5v6408.h>
#include <pi4ioe5v6408_module.h>
#include <tactility/device.h>
#include <tactility/driver.h>
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
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    return ERROR_NONE;
}

extern "C" {

error_t pi4ioe5v6408_set_direction(Device* device, uint8_t bits, TickType_t timeout) {
    auto* parent = device_get_parent(device);
    return i2c_controller_register8_set(parent, GET_CONFIG(device)->reg, PI4_REGISTER_DIRECTION, bits, timeout);
}

error_t pi4ioe5v6408_set_output_level(Device* device, uint8_t bits, TickType_t timeout) {
    auto* parent = device_get_parent(device);
    return i2c_controller_register8_set(parent, GET_CONFIG(device)->reg, PI4_REGISTER_OUTPUT_LEVEL, bits, timeout);
}

error_t pi4ioe5v6408_get_output_level(Device* device, uint8_t* bits, TickType_t timeout) {
    auto* parent = device_get_parent(device);
    return i2c_controller_register8_get(parent, GET_CONFIG(device)->reg, PI4_REGISTER_OUTPUT_LEVEL, bits, timeout);
}

error_t pi4ioe5v6408_set_output_high_impedance(struct Device* device, uint8_t bits, TickType_t timeout) {
    auto* parent = device_get_parent(device);
    return i2c_controller_register8_set(parent, GET_CONFIG(device)->reg, PI4_REGISTER_OUTPUT_HIGH_IMPEDANCE, bits, timeout);
}

error_t pi4ioe5v6408_set_input_default_level(struct Device* device, uint8_t bits, TickType_t timeout) {
    auto* parent = device_get_parent(device);
    return i2c_controller_register8_set(parent, GET_CONFIG(device)->reg, PI4_REGISTER_INPUT_DEFAULT_LEVEL, bits, timeout);
}

error_t pi4ioe5v6408_set_pull_enable(struct Device* device, uint8_t bits, TickType_t timeout) {
    auto* parent = device_get_parent(device);
    return i2c_controller_register8_set(parent, GET_CONFIG(device)->reg, PI4_REGISTER_PULL_ENABLE, bits, timeout);
}

error_t pi4ioe5v6408_set_pull_select(struct Device* device, uint8_t bits, TickType_t timeout) {
    auto* parent = device_get_parent(device);
    return i2c_controller_register8_set(parent, GET_CONFIG(device)->reg, PI4_REGISTER_PULL_SELECT, bits, timeout);
}

error_t pi4ioe5v6408_get_input_level(struct Device* device, uint8_t* bits, TickType_t timeout) {
    auto* parent = device_get_parent(device);
    return i2c_controller_register8_get(parent, GET_CONFIG(device)->reg, PI4_REGISTER_INPUT_LEVEL, bits, timeout);
}

error_t pi4ioe5v6408_set_interrupt_mask(struct Device* device, uint8_t bits, TickType_t timeout) {
    auto* parent = device_get_parent(device);
    return i2c_controller_register8_set(parent, GET_CONFIG(device)->reg, PI4_REGISTER_INTERRUPT_MASK, bits, timeout);
}

error_t pi4ioe5v6408_get_interrupt_level(struct Device* device, uint8_t* bits, TickType_t timeout) {
    auto* parent = device_get_parent(device);
    return i2c_controller_register8_get(parent, GET_CONFIG(device)->reg, PI4_REGISTER_INTERRUPT_LEVEL, bits, timeout);
}

Driver pi4ioe5v6408_driver = {
    .name = "pi4ioe5v6408",
    .compatible = (const char*[]) { "diodes,pi4ioe5v6408", nullptr},
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &pi4ioe5v6408_module,
    .internal = nullptr
};

}