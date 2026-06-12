#include "Tactility/hal/i2c/I2cDevice.h"

#include <cstdint>

namespace tt::hal::i2c {

bool I2cDevice::read(uint8_t* data, size_t dataSize, TickType_t timeout) {
    return i2c_controller_read(controller, address, data, dataSize, timeout) == ERROR_NONE;
}

bool I2cDevice::write(const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    return i2c_controller_write(controller, address, data, dataSize, timeout) == ERROR_NONE;
}

bool I2cDevice::writeRead(const uint8_t* writeData, size_t writeDataSize, uint8_t* readData, size_t readDataSize, TickType_t timeout) {
    return i2c_controller_write_read(controller, address, writeData, writeDataSize, readData, readDataSize, timeout) == ERROR_NONE;
}

bool I2cDevice::writeRegister(uint8_t reg, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    return i2c_controller_write_register(controller, address, reg, data, dataSize, timeout) == ERROR_NONE;
}

bool I2cDevice::readRegister12(uint8_t reg, float& out) const {
    std::uint8_t data[2] = {0};
    if (i2c_controller_read_register(controller, address, reg, data, 2, DEFAULT_TIMEOUT) == ERROR_NONE) {
        out = (data[0] & 0x0F) << 8 | data[1];
        return true;
    } else {
        return false;
    }
}

bool I2cDevice::readRegister14(uint8_t reg, float& out) const {
    std::uint8_t data[2] = {0};
    if (i2c_controller_read_register(controller, address, reg, data, 2, DEFAULT_TIMEOUT) == ERROR_NONE) {
        out = (data[0] & 0x3F) << 8 | data[1];
        return true;
    } else {
        return false;
    }
}

bool I2cDevice::readRegister16(uint8_t reg, uint16_t& out) const {
    std::uint8_t data[2] = {0};
    if (i2c_controller_read_register(controller, address, reg, data, 2, DEFAULT_TIMEOUT) == ERROR_NONE) {
        out = data[0] << 8 | data[1];
        return true;
    } else {
        return false;
    }
}

bool I2cDevice::readRegister8(uint8_t reg, uint8_t& result) const {
    return i2c_controller_write_read(controller, address, &reg, 1, &result, 1, DEFAULT_TIMEOUT) == ERROR_NONE;
}

bool I2cDevice::writeRegister8(uint8_t reg, uint8_t value) const {
    return i2c_controller_write_register(controller, address, reg, &value, 1, DEFAULT_TIMEOUT) == ERROR_NONE;
}

bool I2cDevice::bitOn(uint8_t reg, uint8_t bitmask) const {
    uint8_t state;
    if (readRegister8(reg, state)) {
        state |= bitmask;
        return writeRegister8(reg, state);
    } else {
        return false;
    }
}

bool I2cDevice::bitOff(uint8_t reg, uint8_t bitmask) const {
    uint8_t state;
    if (readRegister8(reg, state)) {
        state &= ~bitmask;
        return writeRegister8(reg, state);
    } else {
        return false;
    }
}

} // namespace tt::hal::i2c
