#pragma once

#include <Tactility/hal/i2c/I2cDevice.h>

#define AXP2101_ADDRESS 0x34

/**
 * References:
 * - https://github.com/m5stack/M5Unified/blob/master/src/utility/AXP2101_Class.cpp
 * - http://file.whycan.com/files/members/6736/AXP2101_Datasheet_V1.0_en_3832.pdf
 */
class Axp2101 final : public tt::hal::i2c::I2cDevice {

public:

    enum ChargeStatus {
        CHARGE_STATUS_CHARGING = 0b01,
        CHARGE_STATUS_DISCHARGING = 0b10,
        CHARGE_STATUS_STANDBY = 0b00
    };

    explicit Axp2101(i2c_port_t port) : I2cDevice(port, AXP2101_ADDRESS) {}

    std::string getName() const override { return "AXP2101"; }
    std::string getDescription() const override { return "Power management with I2C interface."; }

    bool setRegisters(uint8_t* bytePairs, size_t bytePairsSize) const;

    bool getBatteryVoltage(float& vbatMillis) const;
    bool getBatteryPercentage(uint8_t& percentage) const;
    bool getChargeStatus(ChargeStatus& status) const;
    bool isChargingEnabled(bool& enabled) const;
    bool setChargingEnabled(bool enabled) const;

    bool isVBus() const;
    bool getVBusVoltage(float& out) const;
};