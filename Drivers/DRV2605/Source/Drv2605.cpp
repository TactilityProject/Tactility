#include "Drv2605.h"

#include <Tactility/Check.h>
#include <Tactility/Logger.h>

static const auto LOGGER = tt::Logger("DRV2605");

Drv2605::Drv2605(i2c_port_t port, bool autoPlayStartupBuzz) : I2cDevice(port, ADDRESS), autoPlayStartupBuzz(autoPlayStartupBuzz) {
    check(init(), "Initialize DRV2605");

    if (autoPlayStartupBuzz) {
        setWaveFormForBuzz();
        startPlayback();
    }
}

bool Drv2605::init() {
    uint8_t status;
    if (!readRegister8(static_cast<uint8_t>(Register::Status), status)) {
        LOGGER.error("Failed to read status");
        return false;
    }
    status >>= 5;

    ChipId chip_id = static_cast<ChipId>(status);
    if (chip_id != ChipId::DRV2604 && chip_id != ChipId::DRV2604L && chip_id != ChipId::DRV2605 && chip_id != ChipId::DRV2605L) {
        LOGGER.error("Unknown chip id {:02x}", static_cast<uint8_t>(chip_id));
        return false;
    }

    writeRegister(Register::Mode, 0x00); // Get out of standby

    writeRegister(Register::RealtimePlaybackInput, 0x00); // Disable


    setWaveFormForClick();

    // ERM open loop

    uint8_t feedback;
    if (!readRegister(Register::Feedback, feedback)) {
        LOGGER.error("Failed to read feedback");
        return false;
    }

    writeRegister(Register::Feedback, feedback & 0x7F); // N_ERM_LRA off

    bitOnByIndex(static_cast<uint8_t>(Register::Control3), 5); // ERM_OPEN_LOOP on

    return true;
}

void Drv2605::setWaveFormForBuzz() {
    writeRegister(Register::WaveSequence1, 1); // Strong click
    writeRegister(Register::WaveSequence2, 1); // Strong click
    writeRegister(Register::WaveSequence3, 1); // Strong click
    writeRegister(Register::WaveSequence4, 0); // End sequence

    writeRegister(Register::OverdriveTimeOffset, 0); // No overdrive

    writeRegister(Register::SustainTimeOffsetPostivie, 0);
    writeRegister(Register::SustainTimeOffsetNegative, 0);
    writeRegister(Register::BrakeTimeOffset, 0);

    writeRegister(Register::AudioInputLevelMax, 0x64);
}

void Drv2605::setWaveFormForClick() {
    writeRegister(Register::WaveSequence1, 1); // Strong click
    writeRegister(Register::WaveSequence2, 0); // End sequence

    writeRegister(Register::OverdriveTimeOffset, 0); // No overdrive

    writeRegister(Register::SustainTimeOffsetPostivie, 0);
    writeRegister(Register::SustainTimeOffsetNegative, 0);
    writeRegister(Register::BrakeTimeOffset, 0);

    writeRegister(Register::AudioInputLevelMax, 0x64);
}

void Drv2605::setWaveForm(uint8_t slot, uint8_t waveform) {
    writeRegister8(static_cast<uint8_t>(Register::WaveSequence1) + slot, waveform);
}

void Drv2605::selectLibrary(uint8_t library) {
    writeRegister(Register::WaveLibrarySelect, library);
}

void Drv2605::startPlayback() {
    writeRegister(Register::Go, 0x01);
}

void Drv2605::stopPlayback() {
    writeRegister(Register::Go, 0x00);
}

