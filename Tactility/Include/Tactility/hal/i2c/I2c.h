#pragma once

#include "./I2cCompat.h"
#include "Tactility/Lock.h"

#include <Tactility/freertoscompat/RTOS.h>

#include <climits>
#include <string>

namespace tt::hal::i2c {

constexpr TickType_t defaultTimeout = 10 / portTICK_PERIOD_MS;

enum class Status {
    Started,
    Stopped,
    Unknown
};

/** Start the bus for the specified port. */
bool start(i2c_port_t port);

/** Stop the bus for the specified port. */
bool stop(i2c_port_t port);

/** @return true if the bus is started */
bool isStarted(i2c_port_t port);

/** @return name or nullptr */
const char* getName(i2c_port_t port);

/** Read bytes in master mode. */
bool masterRead(i2c_port_t port, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout = defaultTimeout);

/** Read bytes from the specified register in master mode. */
bool masterReadRegister(i2c_port_t port, uint8_t address, uint8_t reg, uint8_t* data, size_t dataSize, TickType_t timeout = defaultTimeout);

/** Write bytes in master mode. */
bool masterWrite(i2c_port_t port, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout = defaultTimeout);

/** Write bytes to a register in master mode */
bool masterWriteRegister(i2c_port_t port, uint8_t address, uint8_t reg, const uint8_t* data, uint16_t dataSize, TickType_t timeout = defaultTimeout);

/**
 * Write multiple values to multiple registers in master mode.
 * The input is as follows: { register1, value1, register2, value2, ... }
 * @return false if any of the write operations failed
 */
bool masterWriteRegisterArray(i2c_port_t port, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout = defaultTimeout);

/** Write bytes and then read the response bytes in master mode*/
bool masterWriteRead(i2c_port_t port, uint8_t address, const uint8_t* writeData, size_t writeDataSize, uint8_t* readData, size_t readDataSize, TickType_t timeout = defaultTimeout);

/** @return true when a device is detected at the specified address */
bool masterHasDeviceAtAddress(i2c_port_t port, uint8_t address, TickType_t timeout = defaultTimeout);

/**
 * The lock for the specified bus.
 * This can be used when calling native I2C functionality outside of Tactility.
 */
Lock& getLock(i2c_port_t port);

} // namespace
