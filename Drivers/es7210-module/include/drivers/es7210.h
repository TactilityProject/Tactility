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
 * @brief ES7210 codec device configuration (set from devicetree).
 *
 * The ES7210 is an input-only (microphone ADC) codec. The I2C bus is the
 * device's parent (per i2c-device.yaml), while the I2S controller carrying
 * the (typically 4-slot TDM) audio data is referenced by name.
 */
struct Es7210Config {
    /** I2C address on the bus (typically 0x40) */
    uint8_t address;
    /** Name of the I2S controller device that carries audio data (e.g. "i2s0") */
    const char* i2s_device_name;
    /** Bitmask of microphones to enable, e.g. ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ... */
    uint8_t mic_selected_mask;
    /**
     * Extra fixed digital gain multiplier applied by audio_stream on top of the ES7210's
     * own hardware ADC gain (already at a hard-coded 30dB default out of a 0..37.5dB
     * range), as an integer percentage (100 = 1.0x / no extra boost). Small MEMS mic
     * capsules can still sound quiet even near max hardware gain; this is for boards where
     * 30dB hardware gain alone isn't enough. devicetree has no float property type, hence
     * the x100 integer encoding.
     */
    uint16_t input_gain_x100;
};

#ifdef __cplusplus
}
#endif
