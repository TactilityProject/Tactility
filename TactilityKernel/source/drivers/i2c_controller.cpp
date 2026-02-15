// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/i2c_controller.h>
#include <tactility/error.h>

#define I2C_DRIVER_API(driver) ((struct I2cControllerApi*)driver->api)

extern "C" {

error_t i2c_controller_read(Device* device, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return I2C_DRIVER_API(driver)->read(device, address, data, dataSize, timeout);
}

error_t i2c_controller_write(Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return I2C_DRIVER_API(driver)->write(device, address, data, dataSize, timeout);
}

error_t i2c_controller_write_read(Device* device, uint8_t address, const uint8_t* writeData, size_t writeDataSize, uint8_t* readData, size_t readDataSize, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return I2C_DRIVER_API(driver)->write_read(device, address, writeData, writeDataSize, readData, readDataSize, timeout);
}

error_t i2c_controller_read_register(Device* device, uint8_t address, uint8_t reg, uint8_t* data, size_t dataSize, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return I2C_DRIVER_API(driver)->read_register(device, address, reg, data, dataSize, timeout);
}

error_t i2c_controller_write_register(Device* device, uint8_t address, uint8_t reg, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return I2C_DRIVER_API(driver)->write_register(device, address, reg, data, dataSize, timeout);
}

error_t i2c_controller_register8_set(Device* device, uint8_t address, uint8_t reg, uint8_t value, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return I2C_DRIVER_API(driver)->write_register(device, address, reg, &value, 1, timeout);
}

error_t i2c_controller_register8_get(Device* device, uint8_t address, uint8_t reg, uint8_t* value, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return I2C_DRIVER_API(driver)->read_register(device, address, reg, value, 1, timeout);
}

error_t i2c_controller_register8_set_bits(Device* device, uint8_t address, uint8_t reg, uint8_t bits_to_set, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    uint8_t data = 0;
    auto error = I2C_DRIVER_API(driver)->read_register(device, address, reg, &data, 1, timeout);
    if (error != ERROR_NONE) return error;
    data |= bits_to_set;
    return I2C_DRIVER_API(driver)->write_register(device, address, reg, &data, 1, timeout);
}

error_t i2c_controller_register8_reset_bits(Device* device, uint8_t address, uint8_t reg, uint8_t bits_to_reset, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    uint8_t data = 0;
    auto error = I2C_DRIVER_API(driver)->read_register(device, address, reg, &data, 1, timeout);
    if (error != ERROR_NONE) return error;
    data &= ~bits_to_reset;
    return I2C_DRIVER_API(driver)->write_register(device, address, reg, &data, 1, timeout);
}

error_t i2c_controller_write_register_array(Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    if (dataSize % 2 != 0) {
        return ERROR_INVALID_ARGUMENT;
    }
    for (int i = 0; i < dataSize; i += 2) {
        error_t error = I2C_DRIVER_API(driver)->write_register(device, address, data[i], &data[i + 1], 1, timeout);
        if (error != ERROR_NONE) return error;
    }
    return ERROR_NONE;
}

error_t i2c_controller_has_device_at_address(Device* device, uint8_t address, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    uint8_t message[2] = { 0, 0 };
    return I2C_DRIVER_API(driver)->write(device, address, message, 2, timeout);
}

const struct DeviceType I2C_CONTROLLER_TYPE {
    .name = "i2c-controller"
};

}
