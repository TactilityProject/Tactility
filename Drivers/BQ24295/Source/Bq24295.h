#pragma once

#include <Tactility/hal/i2c/I2cDevice.h>

#define BQ24295_ADDRESS 0x6BU

class Bq24295 final : public tt::hal::i2c::I2cDevice {


    bool readChargeTermination(uint8_t& out) const;

public:

    std::string getName() const final { return "BQ24295"; }

    std::string getDescription() const final { return "I2C-controlled single cell USB charger"; }

    enum class WatchDogTimer {
        Disabled = 0b000000,
        Enabled40s = 0b010000,
        Enabled80s = 0b100000,
        Enabled160s = 0b110000
    };

    explicit Bq24295(::Device* controller) : I2cDevice(controller, BQ24295_ADDRESS) {}

    bool getWatchDogTimer(WatchDogTimer& out) const;
    bool setWatchDogTimer(WatchDogTimer in) const;

    bool isUsbPowerConnected() const;

    bool setBatFetOn(bool on) const;

    bool getStatus(uint8_t& value) const;
    bool getVersion(uint8_t& value) const;

    void printInfo() const;
};
