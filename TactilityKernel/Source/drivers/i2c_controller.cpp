// SPDX-License-Identifier: Apache-2.0

#include <tactility/driver.h>
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

const struct DeviceType I2C_CONTROLLER_TYPE { 0 };

}
