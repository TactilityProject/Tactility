// SPDX-License-Identifier: Apache-2.0
#include <drivers/drv2605.h>
#include <drv2605_module.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/haptic.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#define TAG "DRV2605"
#define GET_CONFIG(device) (static_cast<const Drv2605Config*>((device)->config))

static const TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

// DRV260x register map (ported from the deprecated HAL's Drv2605.h/.cpp).
static constexpr uint8_t REG_STATUS = 0x00;
static constexpr uint8_t REG_MODE = 0x01;
static constexpr uint8_t REG_REALTIME_PLAYBACK_INPUT = 0x02;
static constexpr uint8_t REG_WAVE_LIBRARY_SELECT = 0x03;
static constexpr uint8_t REG_WAVE_SEQUENCE_1 = 0x04; // WaveSequence1..8 = 0x04..0x0B
static constexpr uint8_t REG_GO = 0x0C;
static constexpr uint8_t REG_OVERDRIVE_TIME_OFFSET = 0x0D;
static constexpr uint8_t REG_SUSTAIN_TIME_OFFSET_POSITIVE = 0x0E;
static constexpr uint8_t REG_SUSTAIN_TIME_OFFSET_NEGATIVE = 0x0F;
static constexpr uint8_t REG_BRAKE_TIME_OFFSET = 0x10;
static constexpr uint8_t REG_AUDIO_INPUT_LEVEL_MAX = 0x13;
static constexpr uint8_t REG_FEEDBACK = 0x1A;
static constexpr uint8_t REG_CONTROL3 = 0x1D;

static constexpr uint8_t FEEDBACK_N_ERM_LRA = 1u << 7;
static constexpr uint8_t CONTROL3_ERM_OPEN_LOOP = 1u << 5;

// 3x strong-click "buzz", matching the deprecated HAL's Drv2605::setWaveFormForBuzz().
static constexpr uint8_t DEFAULT_STARTUP_WAVEFORM[] = { 1, 1, 1, 0 };

extern "C" {

// region HapticApi

static error_t set_waveform(Device* device, uint8_t slot, uint8_t waveform) {
    const auto* config = GET_CONFIG(device);
    auto* i2c = device_get_parent(device);
    return i2c_controller_register8_set(i2c, config->address, static_cast<uint8_t>(REG_WAVE_SEQUENCE_1 + slot), waveform, I2C_TIMEOUT);
}

static error_t select_library(Device* device, uint8_t library) {
    const auto* config = GET_CONFIG(device);
    auto* i2c = device_get_parent(device);
    return i2c_controller_register8_set(i2c, config->address, REG_WAVE_LIBRARY_SELECT, library, I2C_TIMEOUT);
}

static error_t start_playback(Device* device) {
    const auto* config = GET_CONFIG(device);
    auto* i2c = device_get_parent(device);
    return i2c_controller_register8_set(i2c, config->address, REG_GO, 0x01, I2C_TIMEOUT);
}

static error_t stop_playback(Device* device) {
    const auto* config = GET_CONFIG(device);
    auto* i2c = device_get_parent(device);
    return i2c_controller_register8_set(i2c, config->address, REG_GO, 0x00, I2C_TIMEOUT);
}

static constexpr HapticApi DRV2605_HAPTIC_API = {
    .set_waveform = set_waveform,
    .select_library = select_library,
    .start_playback = start_playback,
    .stop_playback = stop_playback,
};

// endregion

// region Driver lifecycle

static error_t write_waveform_sequence(Device* i2c, uint8_t address, const uint8_t* waveform, uint32_t length) {
    for (uint32_t i = 0; i < length && i < 8; i++) {
        error_t error = i2c_controller_register8_set(i2c, address, static_cast<uint8_t>(REG_WAVE_SEQUENCE_1 + i), waveform[i], I2C_TIMEOUT);
        if (error != ERROR_NONE) {
            return error;
        }
    }
    return ERROR_NONE;
}

static error_t start(Device* device) {
    auto* i2c = device_get_parent(device);
    check(device_get_type(i2c) == &I2C_CONTROLLER_TYPE);
    const auto* config = GET_CONFIG(device);

    uint8_t status = 0;
    if (i2c_controller_register8_get(i2c, config->address, REG_STATUS, &status, I2C_TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to read status");
        return ERROR_RESOURCE;
    }

    // Bits[7:5]: 3=DRV2605, 4=DRV2604, 6=DRV2604L, 7=DRV2605L.
    uint8_t chip_id = status >> 5;
    if (chip_id != 3 && chip_id != 4 && chip_id != 6 && chip_id != 7) {
        LOG_E(TAG, "Unknown chip id %02X", chip_id);
        return ERROR_RESOURCE;
    }

    i2c_controller_register8_set(i2c, config->address, REG_MODE, 0x00, I2C_TIMEOUT); // Exit standby
    i2c_controller_register8_set(i2c, config->address, REG_REALTIME_PLAYBACK_INPUT, 0x00, I2C_TIMEOUT); // Disable

    // Baseline single strong-click sequence (matches Drv2605::setWaveFormForClick()).
    write_waveform_sequence(i2c, config->address, (const uint8_t[]) { 1, 0 }, 2);
    i2c_controller_register8_set(i2c, config->address, REG_OVERDRIVE_TIME_OFFSET, 0x00, I2C_TIMEOUT);
    i2c_controller_register8_set(i2c, config->address, REG_SUSTAIN_TIME_OFFSET_POSITIVE, 0x00, I2C_TIMEOUT);
    i2c_controller_register8_set(i2c, config->address, REG_SUSTAIN_TIME_OFFSET_NEGATIVE, 0x00, I2C_TIMEOUT);
    i2c_controller_register8_set(i2c, config->address, REG_BRAKE_TIME_OFFSET, 0x00, I2C_TIMEOUT);
    i2c_controller_register8_set(i2c, config->address, REG_AUDIO_INPUT_LEVEL_MAX, 0x64, I2C_TIMEOUT);

    // ERM open loop.
    i2c_controller_register8_reset_bits(i2c, config->address, REG_FEEDBACK, FEEDBACK_N_ERM_LRA, I2C_TIMEOUT);
    i2c_controller_register8_set_bits(i2c, config->address, REG_CONTROL3, CONTROL3_ERM_OPEN_LOOP, I2C_TIMEOUT);

    if (config->autoplay) {
        const uint8_t* waveform = config->startup_waveform;
        uint32_t length = config->startup_waveform_length;
        if (waveform == nullptr || length == 0) {
            waveform = DEFAULT_STARTUP_WAVEFORM;
            length = sizeof(DEFAULT_STARTUP_WAVEFORM);
        }
        write_waveform_sequence(i2c, config->address, waveform, length);
        i2c_controller_register8_set(i2c, config->address, REG_GO, 0x01, I2C_TIMEOUT);
    }

    return ERROR_NONE;
}

static error_t stop(Device*) {
    return ERROR_NONE;
}

// endregion

Driver drv2605_driver = {
    .name = "drv2605",
    .compatible = (const char*[]) { "ti,drv2605", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &DRV2605_HAPTIC_API,
    .device_type = &HAPTIC_TYPE,
    .owner = &drv2605_module,
    .internal = nullptr
};

}
