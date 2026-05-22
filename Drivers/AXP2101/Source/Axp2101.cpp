#include "Axp2101.h"

bool Axp2101::getBatteryVoltage(float& vbatMillis) const {
    uint8_t data[2] = {0};
    if (tt::hal::i2c::masterReadRegister(port, address, 0x34, data, 2, DEFAULT_TIMEOUT)) {
        // Battery voltage ADC is 13-bit: 5 bits in 0x34, 8 bits in 0x35
        vbatMillis = ((data[0] & 0x1F) << 8 | data[1]);
        return true;
    }
    return false;
}

bool Axp2101::getChargeStatus(ChargeStatus& status) const {
    uint8_t value;
    if (readRegister8(0x01, value)) {
        value = (value >> 5) & 0b11;
        status = (value == 1) ? CHARGE_STATUS_CHARGING : ((value == 2) ? CHARGE_STATUS_DISCHARGING : CHARGE_STATUS_STANDBY);
        return true;
    }
    return false;
}

bool Axp2101::getBatteryPercentage(uint8_t& percentage) const {
    uint8_t value;
    if (readRegister8(0xA4, value)) {
        percentage = value;
        return true;
    }
    return false;
}

bool Axp2101::isChargingEnabled(bool& enabled) const {
    uint8_t value;
    if (readRegister8(0x18, value)) {
        enabled = value & 0b10;
        return true;
    }
    return false;
}

bool Axp2101::setChargingEnabled(bool enabled) const {
    return writeRegister8(0x18, enabled ? 0x00 : 0x02);
}

bool Axp2101::isVBus() const {
    uint8_t value;
    return readRegister8(0x00, value) && (value & 0x20);
}

bool Axp2101::getVBusVoltage(float& out) const {
    if (!isVBus()) {
        return false;
    }
    uint16_t vbus;
    if (readRegister16(0x38, vbus)) {
        out = vbus / 1000.0f;
        return true;
    }
    return false;
}

bool Axp2101::setRegisters(uint8_t* bytePairs, size_t bytePairsSize) const {
    return tt::hal::i2c::masterWriteRegisterArray(port, address, bytePairs, bytePairsSize, DEFAULT_TIMEOUT);
}
