#pragma once

#include <Tactility/hal/i2c/I2cDevice.h>

#define AW9523_ADDRESS 0x58

class Aw9523 : public tt::hal::i2c::I2cDevice {

public:

    explicit Aw9523(::Device* controller) : I2cDevice(controller, AW9523_ADDRESS) {}

    std::string getName() const final { return "AW9523"; }
    std::string getDescription() const final { return "GPIO expander with LED driver and I2C interface."; }

    bool readP0(uint8_t& output) const;
    bool readP1(uint8_t& output) const;
    bool readCTL(uint8_t& output) const;

    bool writeP0(uint8_t value) const;
    bool writeP1(uint8_t value) const;
    bool writeCTL(uint8_t value) const;

    bool bitOnP1(uint8_t bitmask) const;
};
