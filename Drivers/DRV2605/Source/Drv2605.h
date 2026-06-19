#pragma once

#include <Tactility/hal/i2c/I2cDevice.h>

class Drv2605 : public tt::hal::i2c::I2cDevice {

    static constexpr auto ADDRESS = 0x5A;

    bool autoPlayStartupBuzz;

    // Chip IDs
    enum class ChipId {
        DRV2604 = 0x04,     // Has RAM. Doesn't havew licensed ROM library.
        DRV2605 = 0x03,     // Has licensed ROM library. Doesn't have RAM.
        DRV2604L = 0x06,    // Low-voltage variant of the DRV2604.
        DRV2605L = 0x07     // Low-voltage variant of the DRV2605.
    };

    enum class Register {
        Status = 0x00,
        Mode = 0x01,
        RealtimePlaybackInput = 0x02,
        WaveLibrarySelect = 0x03,
        WaveSequence1 = 0x04,
        WaveSequence2 = 0x05,
        WaveSequence3 = 0x06,
        WaveSequence4 = 0x07,
        WaveSequence5 = 0x08,
        WaveSequence6 = 0x09,
        WaveSequence7 = 0x0A,
        WaveSequence8 = 0x0B,
        Go = 0x0C,
        OverdriveTimeOffset = 0x0D,
        SustainTimeOffsetPostivie = 0x0E,
        SustainTimeOffsetNegative = 0x0F,
        BrakeTimeOffset = 0x10,
        AudioControl = 0x11,
        AudioInputLevelMin = 0x12,
        AudioInputLevelMax = 0x13,
        AudioOutputLevelMin = 0x14,
        AudioOutputLevelMax = 0x15,
        RatedVoltage = 0x16,
        OverdriveClampVoltage = 0x17,
        AutoCalibrationCompensation = 0x18,
        AutoCalibrationBackEmf = 0x19,
        Feedback = 0x1A,
        Control1 = 0x1B,
        Control2 = 0x1C,
        Control3 = 0x1D,
        Control4 = 0x1E,
        Vbat = 0x21,
        LraResonancePeriod = 0x22,
    };

    bool writeRegister(Register reg, const uint8_t value) const {
        return writeRegister8(static_cast<uint8_t>(reg), value);
    }

    bool readRegister(Register reg, uint8_t& value) const {
        return readRegister8(static_cast<uint8_t>(reg), value);
    }

public:

    explicit Drv2605(::Device* controller, bool autoPlayStartupBuzz = true);

    std::string getName() const final { return "DRV2605"; }
    std::string getDescription() const final { return "Haptic driver for ERM/LRA with waveform library & auto-resonance tracking"; }

    bool init();

    void setWaveFormForBuzz();
    void setWaveFormForClick();

    /**
     * @param slot a value from 0 to 7
     * @param waveform
     */
    void setWaveForm(uint8_t slot, uint8_t waveform);
    void selectLibrary(uint8_t library);
    void startPlayback();
    void stopPlayback();
};
