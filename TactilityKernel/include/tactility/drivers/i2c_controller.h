// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "gpio.h"

#include <tactility/freertos/freertos.h>
#include <tactility/error.h>

struct Device;

/**
 * @brief API for I2C controller drivers.
 */
struct I2cControllerApi {
    /**
     * @brief Reads data from an I2C device.
     * @param[in] device the I2C controller device
     * @param[in] address the 7-bit I2C address of the slave device
     * @param[out] data the buffer to store the read data
     * @param[in] dataSize the number of bytes to read
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the read operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*read)(struct Device* device, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout);

    /**
     * @brief Writes data to an I2C device.
     * @param[in] device the I2C controller device
     * @param[in] address the 7-bit I2C address of the slave device
     * @param[in] data the buffer containing the data to write
     * @param[in] dataSize the number of bytes to write
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the write operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*write)(struct Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout);

    /**
     * @brief Writes data to then reads data from an I2C device.
     * @param[in] device the I2C controller device
     * @param[in] address the 7-bit I2C address of the slave device
     * @param[in] writeData the buffer containing the data to write
     * @param[in] writeDataSize the number of bytes to write
     * @param[out] readData the buffer to store the read data
     * @param[in] readDataSize the number of bytes to read
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*write_read)(struct Device* device, uint8_t address, const uint8_t* writeData, size_t writeDataSize, uint8_t* readData, size_t readDataSize, TickType_t timeout);

    /**
     * @brief Reads data from a register of an I2C device.
     * @param[in] device the I2C controller device
     * @param[in] address the 7-bit I2C address of the slave device
     * @param[in] reg the register address to read from
     * @param[out] data the buffer to store the read data
     * @param[in] dataSize the number of bytes to read
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the read operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*read_register)(struct Device* device, uint8_t address, uint8_t reg, uint8_t* data, size_t dataSize, TickType_t timeout);

    /**
     * @brief Writes data to a register of an I2C device.
     * @param[in] device the I2C controller device
     * @param[in] address the 7-bit I2C address of the slave device
     * @param[in] reg the register address to write to
     * @param[in] data the buffer containing the data to write
     * @param[in] dataSize the number of bytes to write
     * @param[in] timeout the maximum time to wait for the operation to complete
     * @retval ERROR_NONE when the write operation was successful
     * @retval ERROR_TIMEOUT when the operation timed out
     */
    error_t (*write_register)(struct Device* device, uint8_t address, uint8_t reg, const uint8_t* data, uint16_t dataSize, TickType_t timeout);
};

/**
 * @brief Reads data from an I2C device using the specified controller.
 * @param[in] device the I2C controller device
 * @param[in] address the 7-bit I2C address of the slave device
 * @param[out] data the buffer to store the read data
 * @param[in] dataSize the number of bytes to read
 * @param[in] timeout the maximum time to wait for the operation to complete
 * @retval ERROR_NONE when the read operation was successful
 */
error_t i2c_controller_read(struct Device* device, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout);

/**
 * @brief Writes data to an I2C device using the specified controller.
 * @param[in] device the I2C controller device
 * @param[in] address the 7-bit I2C address of the slave device
 * @param[in] data the buffer containing the data to write
 * @param[in] dataSize the number of bytes to write
 * @param[in] timeout the maximum time to wait for the operation to complete
 * @retval ERROR_NONE when the write operation was successful
 */
error_t i2c_controller_write(struct Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout);

/**
 * @brief Writes data to then reads data from an I2C device using the specified controller.
 * @param[in] device the I2C controller device
 * @param[in] address the 7-bit I2C address of the slave device
 * @param[in] writeData the buffer containing the data to write
 * @param[in] writeDataSize the number of bytes to write
 * @param[out] readData the buffer to store the read data
 * @param[in] readDataSize the number of bytes to read
 * @param[in] timeout the maximum time to wait for the operation to complete
 * @retval ERROR_NONE when the operation was successful
 */
error_t i2c_controller_write_read(struct Device* device, uint8_t address, const uint8_t* writeData, size_t writeDataSize, uint8_t* readData, size_t readDataSize, TickType_t timeout);

/**
 * @brief Reads data from a register of an I2C device using the specified controller.
 * @param[in] device the I2C controller device
 * @param[in] address the 7-bit I2C address of the slave device
 * @param[in] reg the register address to read from
 * @param[out] data the buffer to store the read data
 * @param[in] dataSize the number of bytes to read
 * @param[in] timeout the maximum time to wait for the operation to complete
 * @retval ERROR_NONE when the read operation was successful
 */
error_t i2c_controller_read_register(struct Device* device, uint8_t address, uint8_t reg, uint8_t* data, size_t dataSize, TickType_t timeout);

/**
 * @brief Writes data to a register of an I2C device using the specified controller.
 * @param[in] device the I2C controller device
 * @param[in] address the 7-bit I2C address of the slave device
 * @param[in] reg the register address to write to
 * @param[in] data the buffer containing the data to write
 * @param[in] dataSize the number of bytes to write
 * @param[in] timeout the maximum time to wait for the operation to complete
 * @retval ERROR_NONE when the write operation was successful
 */
error_t i2c_controller_write_register(struct Device* device, uint8_t address, uint8_t reg, const uint8_t* data, uint16_t dataSize, TickType_t timeout);

error_t i2c_controller_register8_set(struct Device* device, uint8_t address, uint8_t reg, uint8_t value, TickType_t timeout);

error_t i2c_controller_register8_get(struct Device* device, uint8_t address, uint8_t reg, uint8_t* value, TickType_t timeout);

error_t i2c_controller_register8_set_bits(struct Device* device, uint8_t address, uint8_t reg, uint8_t bits_to_set, TickType_t timeout);

error_t i2c_controller_register8_reset_bits(struct Device* device, uint8_t address, uint8_t reg, uint8_t bits_to_reset, TickType_t timeout);

/**
 * @brief Writes an array of register-value pairs to an I2C device.
 * @param[in] device the I2C controller device
 * @param[in] address the 7-bit I2C address of the slave device
 * @param[in] data an array of bytes where even indices are register addresses and odd indices are values
 * @param[in] dataSize the number of bytes in the data array (must be even)
 * @param[in] timeout the maximum time to wait for each operation to complete
 * @retval ERROR_NONE when all write operations were successful
 */
error_t i2c_controller_write_register_array(struct Device* device, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout);

/**
 * @brief Checks if an I2C device is present at the specified address.
 * @param[in] device the I2C controller device
 * @param[in] address the 7-bit I2C address to check
 * @param[in] timeout the maximum time to wait for the check to complete
 * @retval ERROR_NONE when a device responded at the address
 */
error_t i2c_controller_has_device_at_address(struct Device* device, uint8_t address, TickType_t timeout);

extern const struct DeviceType I2C_CONTROLLER_TYPE;

#ifdef __cplusplus
}
#endif
