#include "Bq24295.h"
#include <tactility/log.h>

constexpr auto* TAG = "BQ24295";

/** Reference:
 * https://www.ti.com/lit/ds/symlink/bq24295.pdf
 * https://gitlab.com/hamishcunningham/unphonelibrary/-/blob/main/unPhone.h?ref_type=heads
 */
namespace registers {
static const uint8_t CHARGE_TERMINATION = 0x05U; // Datasheet page 35: Charge end/timer cntrl
static const uint8_t OPERATION_CONTROL = 0x07U; // Datasheet page 37: Misc operation control
static const uint8_t STATUS = 0x08U; // Datasheet page 38: System status
static const uint8_t VERSION = 0x0AU; // Datasheet page 38: Vendor/part/revision status
} // namespace registers

bool Bq24295::readChargeTermination(uint8_t& out) const {
    return readRegister8(registers::CHARGE_TERMINATION, out);
}

// region Watchdog
bool Bq24295::getWatchDogTimer(WatchDogTimer& out) const {
    uint8_t value;
    if (readChargeTermination(value)) {
        uint8_t relevant_bits = value & (BIT(4) | BIT(5));
        switch (relevant_bits) {
            case 0b000000:
                out = WatchDogTimer::Disabled;
                return true;
            case 0b010000:
                out = WatchDogTimer::Enabled40s;
                return true;
            case 0b100000:
                out = WatchDogTimer::Enabled80s;
                return true;
            case 0b110000:
                out = WatchDogTimer::Enabled160s;
                return true;
            default:
                return false;
        }
    }

    return false;
}

bool Bq24295::setWatchDogTimer(WatchDogTimer in) const {
    uint8_t value;
    if (readChargeTermination(value)) {
        uint8_t bits_to_set = 0b00110000 & static_cast<uint8_t>(in);
        uint8_t value_cleared = value & 0b11001111;
        uint8_t to_set = bits_to_set | value_cleared;
        LOG_I(TAG, "WatchDogTimer: %02X -> %02X", value, to_set);
        return writeRegister8(registers::CHARGE_TERMINATION, to_set);
    }

    return false;
}


// endregoin

// region Operation Control (REG07)

bool Bq24295::setBatFetOn(bool on) const {
    if (on) {
        // bit 5 low means bat fet is on
        return bitOff(registers::OPERATION_CONTROL, BIT(5));
    } else {
        // bit 5 high means bat fet is off
        return bitOn(registers::OPERATION_CONTROL, BIT(5));
    }
}

// endregion

// region Other

bool Bq24295::getStatus(uint8_t& value) const {
    return readRegister8(registers::STATUS, value);
}

bool Bq24295::isUsbPowerConnected() const {
    uint8_t status;
    if (getStatus(status)) {
        return (status & BIT(2)) != 0U;
    } else {
        return false;
    }
}

bool Bq24295::getVersion(uint8_t& value) const {
    return readRegister8(registers::VERSION, value);
}

void Bq24295::printInfo() const {
    uint8_t version, status, charge_termination;
    if (getStatus(status) && getVersion(version) && readChargeTermination(charge_termination)) {
        LOG_I(TAG, "Version %d, status %02X, charge termination %02X", version, status, charge_termination);
    } else {
        LOG_E(TAG, "Failed to retrieve version and/or status");
    }
}

// endregion