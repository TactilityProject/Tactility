// SPDX-License-Identifier: Apache-2.0

#include <Tactility/drivers/I2cController.h>
#include <Tactility/Driver.h>

#define I2C_DRIVER_API(driver) ((struct I2cControllerApi*)driver->api)

extern "C" {

bool i2c_controller_read(Device* device, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return I2C_DRIVER_API(driver)->read(device, address, data, dataSize, timeout);
}

bool i2c_controller_write(Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return I2C_DRIVER_API(driver)->write(device, address, data, dataSize, timeout);
}

bool i2c_controller_write_read(Device* device, uint8_t address, const uint8_t* write_data, size_t write_data_size, uint8_t* read_data, size_t read_data_size, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return I2C_DRIVER_API(driver)->write_read(device, address, write_data, write_data_size, read_data, read_data_size, timeout);
}

const struct DeviceType I2C_CONTROLLER_TYPE { 0 };

}
