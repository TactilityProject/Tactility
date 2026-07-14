// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <tactility/error.h>
#include <tactility/drivers/audio_codec.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PDM MEMS microphone configuration.
 *
 * Covers any PDM mic with no I2C/register interface -- just I2S PDM RX -- e.g. SPM1423.
 * There is no I2C parent; the device sits standalone in the devicetree and only
 * references the I2S controller by name. PDM RX is hardware-restricted to I2S
 * controller 0 on ESP32 targets, so i2s_device must resolve to that controller.
 */
struct PdmMicConfig {
    /** I2S controller device that carries audio data */
    struct Device* i2s_device;
    /** Number of PDM mics wired (1 = mono, 2 = stereo) */
    uint8_t channels;
    /**
     * Fixed digital gain multiplier applied to this mic's samples by audio_stream, as an
     * integer percentage (100 = 1.0x / no boost, 400 = 4.0x). PDM mics have no hardware
     * gain control, so this is the only way to compensate for a capsule that's quiet at
     * the ADC's full-scale output. devicetree has no float property type, hence the
     * percent-scaled integer encoding. M5Stack's own SPM1423 reference firmware uses a
     * software multiplier of 4 (i.e. 400 here).
     */
    uint16_t input_gain_percent;
};

#ifdef __cplusplus
}
#endif
