#pragma once

#include <tactility/hal/Device.h>
#include "I2c.h"

namespace tt::hal::i2c {

/**
 * Represents an I2C peripheral at a specific port and address.
 * It helps to read and write registers.
 *
 * All read and write calls are thread-safe.
 */
class I2cDevice : public Device {

protected:

    i2c_port_t port;
    uint8_t address;

    static constexpr TickType_t DEFAULT_TIMEOUT = 1000 / portTICK_PERIOD_MS;

    bool read(uint8_t* data, size_t dataSize, TickType_t timeout = DEFAULT_TIMEOUT);
    bool write(const uint8_t* data, uint16_t dataSize, TickType_t timeout = DEFAULT_TIMEOUT);
    bool writeRead(const uint8_t* writeData, size_t writeDataSize, uint8_t* readData, size_t readDataSize, TickType_t timeout = DEFAULT_TIMEOUT);
    bool readRegister8(uint8_t reg, uint8_t& result) const;
    bool writeRegister(uint8_t reg, const uint8_t* data, uint16_t dataSize, TickType_t timeout = DEFAULT_TIMEOUT);
    bool writeRegister8(uint8_t reg, uint8_t value) const;
    bool readRegister12(uint8_t reg, float& out) const;
    bool readRegister14(uint8_t reg, float& out) const;
    bool readRegister16(uint8_t reg, uint16_t& out) const;
    bool bitOn(uint8_t reg, uint8_t bitmask) const;
    bool bitOff(uint8_t reg, uint8_t bitmask) const;
    bool bitOnByIndex(uint8_t reg, uint8_t index) const { return bitOn(reg, 1 << index); }
    bool bitOffByIndex(uint8_t reg, uint8_t index) const { return bitOff(reg, 1 << index); }

public:

    explicit I2cDevice(i2c_port_t port, uint32_t address) : port(port), address(address) {}

    Type getType() const override { return Type::I2c; }

    i2c_port_t getPort() const { return port; }

    uint8_t getAddress() const { return address; }
};

} // namespace tt::hal::i2c
