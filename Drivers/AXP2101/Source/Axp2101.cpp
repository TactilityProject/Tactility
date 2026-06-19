#include "Axp2101.h"

bool Axp2101::getBatteryVoltage(float& vbatMillis) const {
    return readRegister14(0x34, vbatMillis);
}

bool Axp2101::getChargeStatus(ChargeStatus& status) const {
    uint8_t value;
    if (readRegister8(0x01, value)) {
        value = (value >> 5) & 0b11;
        status = (value == 1) ? CHARGE_STATUS_CHARGING : ((value == 2) ? CHARGE_STATUS_DISCHARGING : CHARGE_STATUS_STANDBY);
        return true;
    } else {
        return false;
    }
}

bool Axp2101::isChargingEnabled(bool& enabled) const {
    uint8_t value;
    if (readRegister8(0x18, value)) {
        enabled = value & 0b10;
        return true;
    } else {
        return false;
    }
}

bool Axp2101::setChargingEnabled(bool enabled) const {
    uint8_t value;
    if (readRegister8(0x18, value)) {
        return writeRegister8(0x18, (value & 0xFD) | (enabled << 1));
    } else {
        return false;
    }
}

bool Axp2101::isVBus() const {
    uint8_t value;
    return readRegister8(0x00, value) && (value & 0x20);
}

bool Axp2101::getVBusVoltage(float& out) const {
    if (!isVBus()) {
        return false;
    } else {
        float vbus;
        if (readRegister14(0x38, vbus) && vbus < 16375) {
            out = vbus / 1000.0f;
            return true;
        } else {
            return false;
        }
    }
}

bool Axp2101::setRegisters(uint8_t* bytePairs, size_t bytePairsSize) const {
    return i2c_controller_write_register_array(controller, address, bytePairs, bytePairsSize, DEFAULT_TIMEOUT) == ERROR_NONE;
}
