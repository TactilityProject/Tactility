#pragma once

#include <Tactility/hal/i2c/I2cDevice.h>

constexpr auto BQ25896_ADDRESS = 0x6b;

class Bq25896 final : public tt::hal::i2c::I2cDevice {

public:

    explicit Bq25896(::Device* controller) : I2cDevice(controller, BQ25896_ADDRESS) {
        powerOn();
    }

    std::string getName() const override { return "BQ25896"; }

    std::string getDescription() const override { return "I2C 1 cell 3A buck battery charger with power path and PSEL"; }

    void powerOff();

    void powerOn();
};
