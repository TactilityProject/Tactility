#include "Bq27220.h"
#include <tactility/log.h>

#include "esp_sleep.h"

constexpr auto* TAG = "BQ27220";

#define ARRAYSIZE(a) (sizeof(a) / sizeof(*(a)))

static uint8_t highByte(const uint16_t word) { return (word >> 8) & 0xFF; }
static uint8_t lowByte(const uint16_t word) { return word & 0xFF; }
static constexpr void swapEndianess(uint16_t &word) { word = (lowByte(word) << 8) | highByte(word); }

namespace registers {
    static const uint16_t SUBCMD_CTRL_STATUS                    = 0x0000U;
    static const uint16_t SUBCMD_DEVICE_NUMBER                  = 0x0001U;
    static const uint16_t SUBCMD_FW_VERSION                     = 0x0002U;
    static const uint16_t SUBCMD_HW_VERSION                     = 0x0003U;
    static const uint16_t SUBCMD_BOARD_OFFSET                   = 0x0009U;
    static const uint16_t SUBCMD_CC_OFFSET                      = 0x000AU;
    static const uint16_t SUBCMD_CC_OFFSET_SAVE                 = 0x000BU;
    static const uint16_t SUBCMD_OCV_CMD                        = 0x000CU;
    static const uint16_t SUBCMD_BAT_INSERT                     = 0x000DU;
    static const uint16_t SUBCMD_BAT_REMOVE                     = 0x000EU;
    static const uint16_t SUBCMD_SET_SNOOZE                     = 0x0013U;
    static const uint16_t SUBCMD_CLEAR_SNOOZE                   = 0x0014U;
    static const uint16_t SUBCMD_SET_PROFILE_1                  = 0x0015U;
    static const uint16_t SUBCMD_SET_PROFILE_2                  = 0x0016U;
    static const uint16_t SUBCMD_SET_PROFILE_3                  = 0x0017U;
    static const uint16_t SUBCMD_SET_PROFILE_4                  = 0x0018U;
    static const uint16_t SUBCMD_SET_PROFILE_5                  = 0x0019U;
    static const uint16_t SUBCMD_SET_PROFILE_6                  = 0x001AU;
    static const uint16_t SUBCMD_CAL_TOGGLE                     = 0x002DU;
    static const uint16_t SUBCMD_SEALED                         = 0x0030U;
    static const uint16_t SUBCMD_RESET                          = 0x0041U;
    static const uint16_t SUBCMD_EXIT_CAL                       = 0x0080U;
    static const uint16_t SUBCMD_ENTER_CAL                      = 0x0081U;
    static const uint16_t SUBCMD_ENTER_CFG_UPDATE               = 0x0090U;
    static const uint16_t SUBCMD_EXIT_CFG_UPDATE_REINIT         = 0x0091U;
    static const uint16_t SUBCMD_EXIT_CFG_UPDATE                = 0x0092U;
    static const uint16_t SUBCMD_RETURN_TO_ROM                  = 0x0F00U;

    static const uint8_t CMD_CONTROL                            = 0x00U;
    static const uint8_t CMD_AT_RATE                            = 0x02U;
    static const uint8_t CMD_AT_RATE_TIME_TO_EMPTY              = 0x04U;
    static const uint8_t CMD_TEMPERATURE                        = 0x06U;
    static const uint8_t CMD_VOLTAGE                            = 0x08U;
    static const uint8_t CMD_BATTERY_STATUS                     = 0x0AU;
    static const uint8_t CMD_CURRENT                            = 0x0CU;
    static const uint8_t CMD_REMAINING_CAPACITY                 = 0x10U;
    static const uint8_t CMD_FULL_CHARGE_CAPACITY               = 0x12U;
    static const uint8_t CMD_AVG_CURRENT                        = 0x14U;
    static const uint8_t CMD_TIME_TO_EMPTY                      = 0x16U;
    static const uint8_t CMD_TIME_TO_FULL                       = 0x18U;
    static const uint8_t CMD_STANDBY_CURRENT                    = 0x1AU;
    static const uint8_t CMD_STANDBY_TIME_TO_EMPTY              = 0x1CU;
    static const uint8_t CMD_MAX_LOAD_CURRENT                   = 0x1EU;
    static const uint8_t CMD_MAX_LOAD_TIME_TO_EMPTY             = 0x20U;
    static const uint8_t CMD_RAW_COULOMB_COUNT                  = 0x22U;
    static const uint8_t CMD_AVG_POWER                          = 0x24U;
    static const uint8_t CMD_INTERNAL_TEMPERATURE               = 0x28U;
    static const uint8_t CMD_CYCLE_COUNT                        = 0x2AU;
    static const uint8_t CMD_STATE_OF_CHARGE                    = 0x2CU;
    static const uint8_t CMD_STATE_OF_HEALTH                    = 0x2EU;
    static const uint8_t CMD_CHARGE_VOLTAGE                     = 0x30U;
    static const uint8_t CMD_CHARGE_CURRENT                     = 0x32U;
    static const uint8_t CMD_BTP_DISCHARGE_SET                  = 0x34U;
    static const uint8_t CMD_BTP_CHARGE_SET                     = 0x36U;
    static const uint8_t CMD_OPERATION_STATUS                   = 0x3AU;
    static const uint8_t CMD_DESIGN_CAPACITY                    = 0x3CU;
    static const uint8_t CMD_SELECT_SUBCLASS                    = 0x3EU;
    static const uint8_t CMD_MAC_DATA                           = 0x40U;
    static const uint8_t CMD_MAC_DATA_SUM                       = 0x60U;
    static const uint8_t CMD_MAC_DATA_LEN                       = 0x61U;
    static const uint8_t CMD_ANALOG_COUNT                       = 0x79U;
    static const uint8_t CMD_RAW_CURRENT                        = 0x7AU;
    static const uint8_t CMD_RAW_VOLTAGE                        = 0x7CU;
    static const uint8_t CMD_RAW_INTERNAL_TEMPERATURE           = 0x7EU;
    static const uint8_t MAC_BUFFER_START                       = 0x40U;
    static const uint8_t MAC_BUFFER_END                         = 0x5FU;
    static const uint8_t MAC_DATA_SUM                           = 0x60U;
    static const uint8_t MAC_DATA_LEN                           = 0x61U;
    static const uint8_t ROM_START                              = 0x3EU;

    static const uint16_t ROM_FULL_CHARGE_CAPACITY              = 0x929DU;
    static const uint16_t ROM_DESIGN_CAPACITY                   = 0x929FU;
    static const uint16_t ROM_OPERATION_CONFIG_A                = 0x9206U;
    static const uint16_t ROM_OPERATION_CONFIG_B                = 0x9208U;

} // namespace registers

bool Bq27220::configureCapacity(uint16_t designCapacity, uint16_t fullChargeCapacity) {
    return performConfigUpdate([this, designCapacity, fullChargeCapacity]() {
        // Set the design capacity
        if (!writeConfig16(registers::ROM_DESIGN_CAPACITY, designCapacity)) {
            LOG_E(TAG, "Failed to set design capacity!");
            return false;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);

        // Set full charge capacity
        if (!writeConfig16(registers::ROM_FULL_CHARGE_CAPACITY, fullChargeCapacity)) {
            LOG_E(TAG, "Failed to set full charge capacity!");
            return false;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);

        return true;
    });
}

bool Bq27220::getVoltage(uint16_t &value) {
    if (readRegister16(registers::CMD_VOLTAGE, value)) {
        swapEndianess(value);
        return true;
    }
    return false;
}

bool Bq27220::getCurrent(int16_t &value) {
    uint16_t u16 = 0;
    if (readRegister16(registers::CMD_CURRENT, u16)) {
        swapEndianess(u16);
        value = (int16_t)u16;
        return true;
    }
    return false;
}

bool Bq27220::getBatteryStatus(BatteryStatus &batt_sta) {
    if (readRegister16(registers::CMD_BATTERY_STATUS, batt_sta.full)) {
        swapEndianess(batt_sta.full);
        return true;
    }
    return false;
}

bool Bq27220::getOperationStatus(OperationStatus &oper_sta) {
    if (readRegister16(registers::CMD_OPERATION_STATUS, oper_sta.full)) {
        swapEndianess(oper_sta.full);
        return true;
    }
    return false;
}

bool Bq27220::getTemperature(uint16_t &value) {
    if (readRegister16(registers::CMD_TEMPERATURE, value)) {
        swapEndianess(value);
        return true;
    }
    return false;
}

bool Bq27220::getFullChargeCapacity(uint16_t &value) {
    if (readRegister16(registers::CMD_FULL_CHARGE_CAPACITY, value)) {
        swapEndianess(value);
        return true;
    }
    return false;
}

bool Bq27220::getDesignCapacity(uint16_t &value) {
    if (readRegister16(registers::CMD_DESIGN_CAPACITY, value)) {
        swapEndianess(value);
        return true;
    }
    return false;
}

bool Bq27220::getRemainingCapacity(uint16_t &value) {
    if (readRegister16(registers::CMD_REMAINING_CAPACITY, value)) {
        swapEndianess(value);
        return true;
    }
    return false;
}

bool Bq27220::getStateOfCharge(uint16_t &value) {
    if (readRegister16(registers::CMD_STATE_OF_CHARGE, value)) {
        swapEndianess(value);
        return true;
    }
    return false;
}

bool Bq27220::getStateOfHealth(uint16_t &value) {
    if (readRegister16(registers::CMD_STATE_OF_HEALTH, value)) {
        swapEndianess(value);
        return true;
    }
    return false;
}

bool Bq27220::getChargeVoltageMax(uint16_t &value) {
    if (readRegister16(registers::CMD_CHARGE_VOLTAGE, value)) {
        swapEndianess(value);
        return true;
    }
    return false;
}

bool Bq27220::unsealDevice() {
    uint8_t cmd1[] = {0x00, 0x14, 0x04};
    if (!write(cmd1, ARRAYSIZE(cmd1))) {
        return false;
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
    uint8_t cmd2[] = {0x00, 0x72, 0x36};
    if (!write(cmd2, ARRAYSIZE(cmd2))) {
        return false;
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
    return true;
}

bool Bq27220::unsealFullAccess()
{
    uint8_t buffer[3];
    buffer[0] = 0x00;
    buffer[1] = lowByte((accessKey >> 24));
    buffer[2] = lowByte((accessKey >> 16));
    if (!write(buffer, ARRAYSIZE(buffer))) {
        return false;
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
    buffer[1] = lowByte((accessKey >> 8));
    buffer[2] = lowByte((accessKey));
    if (!write(buffer, ARRAYSIZE(buffer))) {
        return false;
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
    return true;
}

bool Bq27220::exitSealMode() {
    return sendSubCommand(registers::SUBCMD_SEALED);
}

bool Bq27220::sendSubCommand(uint16_t subCmd, bool waitConfirm)
{
    uint8_t buffer[3];
    buffer[0] = 0x00;
    buffer[1] = lowByte(subCmd);
    buffer[2] = highByte(subCmd);
    if (!write(buffer, ARRAYSIZE(buffer))) {
        return false;
    }
    if (!waitConfirm) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        return true;
    }
    constexpr uint8_t statusReg = 0x00;
    int waitCount = 20;
    vTaskDelay(10 / portTICK_PERIOD_MS);
    while (waitCount--) {
        writeRead(&statusReg, 1, buffer, 2);
        uint16_t *value = reinterpret_cast<uint16_t *>(buffer);
        if (*value == 0xFFA5) {
            return true;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    LOG_E(TAG, "Subcommand 0x%04X failed!", subCmd);
    return false;
}

bool Bq27220::writeConfig16(uint16_t address, uint16_t value) {
    constexpr uint8_t fixedDataLength = 0x06;
    const uint8_t msbAccessValue = highByte(address);
    const uint8_t lsbAccessValue = lowByte(address);

    // Write to access the MSB of Capacity
    writeRegister8(registers::ROM_START, msbAccessValue);

    // Write to access the LSB of Capacity
    writeRegister8(registers::ROM_START + 1, lsbAccessValue);

    // Write two Capacity bytes starting from 0x40
    uint8_t valueMsb = highByte(value);
    uint8_t valueLsb = lowByte(value);
    uint8_t configRaw[] = {valueMsb, valueLsb};
    writeRegister(registers::MAC_BUFFER_START, configRaw, 2);
    // Calculate new checksum
    uint8_t checksum = 0xFF - ((msbAccessValue + lsbAccessValue + valueMsb + valueLsb) & 0xFF);

    // Write new checksum (0x60)
    writeRegister8(registers::MAC_DATA_SUM, checksum);

    // Write the block length
    writeRegister8(registers::MAC_DATA_LEN, fixedDataLength);

    return true;
}

bool Bq27220::configPreamble(bool &isSealed) {
    int timeout = 0;
    OperationStatus status;

    // Check access settings
    if(!getOperationStatus(status)) {
        LOG_E(TAG, "Cannot read initial operation status!");
        return false;
    }

    if (status.reg.SEC == OperationStatusSecSealed) {
        isSealed = true;
        if (!unsealDevice()) {
            LOG_E(TAG, "Unsealing device failure!");
            return false;
        }
    }

    if (status.reg.SEC != OperationStatusSecFull) {
        if (!unsealFullAccess()) {
            LOG_E(TAG, "Unsealing full access failure!");
            return false;
        }
    }

    // Send ENTER_CFG_UPDATE command (0x0090)
    if (!sendSubCommand(registers::SUBCMD_ENTER_CFG_UPDATE)) {
        LOG_E(TAG, "Config Update Subcommand failure!");
    }

    // Confirm CFUPDATE mode by polling the OperationStatus() register until Bit 2 is set.
    bool isConfigUpdate = false;
    for (timeout = 30; timeout; --timeout) {
        getOperationStatus(status);
        if (status.reg.CFGUPDATE) {
            isConfigUpdate = true;
            break;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    if (!isConfigUpdate) {
        LOG_E(TAG, "Update Mode timeout, maybe the access key for full permissions is invalid!");
        return false;
    }

    return true;
}

bool Bq27220::configEpilouge(const bool isSealed) {
    int timeout = 0;
    OperationStatus status;

    // Exit CFUPDATE mode by sending the EXIT_CFG_UPDATE_REINIT (0x0091) or EXIT_CFG_UPDATE (0x0092) command
    sendSubCommand(registers::SUBCMD_EXIT_CFG_UPDATE_REINIT);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    // Confirm that CFUPDATE mode has been exited by polling the OperationStatus() register until bit 2 is cleared
    for (timeout = 60; timeout; --timeout) {
        getOperationStatus(status);
        if (!status.reg.CFGUPDATE) {
            break;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    if (timeout == 0) {
        LOG_E(TAG, "Timed out waiting to exit update mode.");
        return false;
    }

    // If the device was previously in SEALED state, return to SEALED mode by sending the Control(0x0030) subcommand
    if (isSealed) {
        LOG_D(TAG, "Restore Safe Mode!");
        exitSealMode();
    }
    return true;
}
