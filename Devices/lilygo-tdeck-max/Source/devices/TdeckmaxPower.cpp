#include "TdeckmaxPower.h"

#include <tactility/log.h>
#include <tactility/drivers/i2c_controller.h>

constexpr auto* TAG = "TdeckmaxPower";

// SY6970 charger (newer T-Deck Max revisions; older boards use BQ25896 @ 0x6B).
// Register map is BQ25896-compatible for everything used here (see NOTES.md).
static constexpr uint8_t SY6970_ADDRESS = 0x6A;
static constexpr uint8_t SY6970_REG_03 = 0x03; // bit4 CHG_CONFIG: charge enable
static constexpr uint8_t SY6970_REG_04 = 0x04; // bits[6:0] ICHG: charge current, 64 mA/LSB
static constexpr uint8_t SY6970_REG_06 = 0x06; // bits[7:2] VREG: charge voltage, 3840 mV + n*16 mV
static constexpr uint8_t SY6970_REG_07 = 0x07; // bits[5:4] WATCHDOG: I2C watchdog timer
static constexpr uint8_t SY6970_REG_09 = 0x09;
static constexpr uint8_t SY6970_REG_0B = 0x0B; // read-only status
static constexpr uint8_t SY6970_CHG_CONFIG = 1 << 4;
static constexpr uint8_t SY6970_BATFET_DIS = 1 << 5; // Force BATFET off = ship mode

static constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

// Charge parameters from the vendor factory firmware (XPowersLib SY6970 init):
// watchdog off, 4288 mV target, 1024 mA fast-charge current. The I2C watchdog
// must be disabled first, otherwise the chip reverts custom register values to
// defaults after the watchdog expires. Input current limit (REG00) is left at
// the hardware PSEL/ILIM default. Non-fatal: charging still works on chip
// defaults if any write fails.
bool TdeckmaxPower::configureCharger() const {
    if (i2c_controller_register8_reset_bits(i2c, SY6970_ADDRESS, SY6970_REG_07, 0x30, I2C_TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to disable SY6970 watchdog");
        return false;
    }

    uint8_t reg06 = 0;
    if (i2c_controller_register8_get(i2c, SY6970_ADDRESS, SY6970_REG_06, &reg06, I2C_TIMEOUT) != ERROR_NONE ||
        i2c_controller_register8_set(i2c, SY6970_ADDRESS, SY6970_REG_06, (reg06 & 0x03) | (28 << 2), I2C_TIMEOUT) != ERROR_NONE) { // 3840 + 28*16 = 4288 mV
        LOG_E(TAG, "Failed to set SY6970 charge voltage");
        return false;
    }

    uint8_t reg04 = 0;
    if (i2c_controller_register8_get(i2c, SY6970_ADDRESS, SY6970_REG_04, &reg04, I2C_TIMEOUT) != ERROR_NONE ||
        i2c_controller_register8_set(i2c, SY6970_ADDRESS, SY6970_REG_04, (reg04 & 0x80) | 0x10, I2C_TIMEOUT) != ERROR_NONE) { // 16 * 64 = 1024 mA
        LOG_E(TAG, "Failed to set SY6970 charge current");
        return false;
    }

    LOG_I(TAG, "SY6970 configured: 4288 mV / 1024 mA, watchdog off");
    return true;
}

bool TdeckmaxPower::isAllowedToCharge() const {
    uint8_t reg03 = 0;
    if (i2c_controller_register8_get(i2c, SY6970_ADDRESS, SY6970_REG_03, &reg03, I2C_TIMEOUT) != ERROR_NONE) {
        return false;
    }
    return (reg03 & SY6970_CHG_CONFIG) != 0;
}

void TdeckmaxPower::setAllowedToCharge(bool canCharge) {
    error_t result;
    if (canCharge) {
        result = i2c_controller_register8_set_bits(i2c, SY6970_ADDRESS, SY6970_REG_03, SY6970_CHG_CONFIG, I2C_TIMEOUT);
    } else {
        result = i2c_controller_register8_reset_bits(i2c, SY6970_ADDRESS, SY6970_REG_03, SY6970_CHG_CONFIG, I2C_TIMEOUT);
    }
    if (result != ERROR_NONE) {
        LOG_E(TAG, "Failed to set SY6970 charge enable");
    }
}

bool TdeckmaxPower::supportsMetric(MetricType type) const {
    switch (type) {
        using enum MetricType;
        case IsCharging:
        case Current:
        case BatteryVoltage:
        case ChargeLevel:
            return true;
        default:
            return false;
    }
}

bool TdeckmaxPower::getMetric(MetricType type, MetricData& data) {
    uint16_t u16 = 0;
    int16_t s16 = 0;
    switch (type) {
        using enum MetricType;
        case IsCharging: {
            // REG0B CHRG_STAT bits[4:3]: 0 = not charging, 1 = pre-charge,
            // 2 = fast charge, 3 = charge done. Only 1|2 count as charging —
            // "done" must not (XPowersLib had this inverted once). Fall back to
            // the fuel gauge's discharge flag if the charger doesn't answer.
            uint8_t reg0b = 0;
            if (i2c_controller_register8_get(i2c, SY6970_ADDRESS, SY6970_REG_0B, &reg0b, I2C_TIMEOUT) == ERROR_NONE) {
                uint8_t charge_status = (reg0b >> 3) & 0x03;
                data.valueAsBool = charge_status == 1 || charge_status == 2;
                return true;
            }
            Bq27220::BatteryStatus status;
            if (gauge->getBatteryStatus(status)) {
                data.valueAsBool = !status.reg.DSG;
                return true;
            }
            return false;
        }
        case Current:
            if (gauge->getCurrent(s16)) {
                data.valueAsInt32 = s16;
                return true;
            }
            return false;
        case BatteryVoltage:
            if (gauge->getVoltage(u16)) {
                data.valueAsUint32 = u16;
                return true;
            }
            return false;
        case ChargeLevel:
            if (gauge->getStateOfCharge(u16)) {
                data.valueAsUint8 = u16;
                return true;
            }
            return false;
        default:
            return false;
    }
}

void TdeckmaxPower::powerOff() {
    // Ship mode: force the charger's BATFET off (REG09 bit5). Mirrors the vendor
    // XPowersLib PowersSY6970::shutdown(). Note: this only fully powers the board
    // down when running on battery with USB unplugged.
    LOG_I(TAG, "Power off (SY6970 BATFET_DIS)");
    if (i2c_controller_register8_set_bits(i2c, SY6970_ADDRESS, SY6970_REG_09, SY6970_BATFET_DIS, pdMS_TO_TICKS(50)) != ERROR_NONE) {
        LOG_E(TAG, "Failed to write SY6970 shutdown register");
    }
}
