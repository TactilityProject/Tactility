#pragma once

#include <Tactility/hal/i2c/I2cDevice.h>

#define BQ27220_ADDRESS 0x55

class Bq27220 final : public tt::hal::i2c::I2cDevice {

    uint32_t accessKey;

    bool unsealDevice();
    bool unsealFullAccess();
    bool exitSealMode();
    bool sendSubCommand(uint16_t subCmd, bool waitConfirm = false);
    bool writeConfig16(uint16_t address, uint16_t value);
    bool configPreamble(bool &isSealed);
    bool configEpilouge(bool isSealed);

    template<typename T>
    bool performConfigUpdate(T configUpdateFunc)
    {
        bool isSealed = false;

        if (!configPreamble(isSealed)) {
            return false;
        }
        bool result = configUpdateFunc();
        configEpilouge(isSealed);

        return result;
    }

public:
    // Register structures lifted from
    // https://github.com/Xinyuan-LilyGO/T-Deck-Pro/blob/master/lib/BQ27220/bq27220.h
    // Copyright (c) 2025 Liygo / Shenzhen Xinyuan Electronic Technology Co., Ltd

    union BatteryStatus {
        struct
        {
            // Low byte, Low bit first
            uint16_t DSG        : 1; /**< The device is in DISCHARGE */
            uint16_t SYSDWN     : 1; /**< System down bit indicating the system should shut down */
            uint16_t TDA        : 1; /**< Terminate Discharge Alarm */
            uint16_t BATTPRES   : 1; /**< Battery Present detected */
            uint16_t AUTH_GD    : 1; /**< Detect inserted battery */
            uint16_t OCVGD      : 1; /**< Good OCV measurement taken */
            uint16_t TCA        : 1; /**< Terminate Charge Alarm */
            uint16_t RSVD       : 1; /**< Reserved */
            // High byte, Low bit first
            uint16_t CHGING     : 1; /**< Charge inhibit */
            uint16_t FC         : 1; /**< Full-charged is detected */
            uint16_t OTD        : 1; /**< Overtemperature in discharge condition is detected */
            uint16_t OTC        : 1; /**< Overtemperature in charge condition is detected */
            uint16_t SLEEP      : 1; /**< Device is operating in SLEEP mode when set */
            uint16_t OCVFALL    : 1; /**< Status bit indicating that the OCV reading failed due to current */
            uint16_t OCVCOMP    : 1; /**< An OCV measurement update is complete */
            uint16_t FD         : 1; /**< Full-discharge is detected */
        } reg;
        uint16_t full;
    };

    enum OperationStatusSec {
        OperationStatusSecSealed = 0b11,
        OperationStatusSecUnsealed = 0b10,
        OperationStatusSecFull = 0b01,
    };

    union OperationStatus {
        struct
        {
            // Low byte, Low bit first
            bool CALMD      : 1; /**< Calibration mode enabled */
            uint8_t SEC     : 2; /**< Current security access */
            bool EDV2       : 1; /**< EDV2 threshold exceeded */
            bool VDQ        : 1; /**< Indicates if Current discharge cycle is NOT qualified or qualified for an FCC updated */
            bool INITCOMP   : 1; /**< gauge initialization is complete */
            bool SMTH       : 1; /**< RemainingCapacity is scaled by smooth engine */
            bool BTPINT     : 1; /**< BTP threshold has been crossed */
            // High byte, Low bit first
            uint8_t RSVD1   : 2; /**< Reserved */
            bool CFGUPDATE  : 1; /**< Gauge is in CONFIG UPDATE mode */
            uint8_t RSVD0   : 5; /**< Reserved */
        } reg;
        uint16_t full;
    };

    std::string getName() const override { return "BQ27220"; }

    std::string getDescription() const override { return "I2C-controlled CEDV battery fuel gauge"; }

    explicit Bq27220(::Device* controller) : I2cDevice(controller, BQ27220_ADDRESS), accessKey(0xFFFFFFFF) {}

    bool configureCapacity(uint16_t designCapacity, uint16_t fullChargeCapacity);
    bool getVoltage(uint16_t &value);
    bool getCurrent(int16_t &value);
    bool getBatteryStatus(BatteryStatus &batt_sta);
    bool getOperationStatus(OperationStatus &oper_sta);
    bool getTemperature(uint16_t &value);
    bool getFullChargeCapacity(uint16_t &value);
    bool getDesignCapacity(uint16_t &value);
    bool getRemainingCapacity(uint16_t &value);
    bool getStateOfCharge(uint16_t &value);
    bool getStateOfHealth(uint16_t &value);
    bool getChargeVoltageMax(uint16_t &value);
};
