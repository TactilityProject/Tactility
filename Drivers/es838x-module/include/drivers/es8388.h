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
 * @brief ES8388 codec device configuration.
 *
 * The ES8388 is wired as both control (I2C) and audio (I2S) device: the I2C
 * bus is the device's parent (per i2c-device.yaml), while the I2S controller
 * is referenced via a devicetree phandle.
 */
struct Es8388Config {
    /** I2C address on the bus (typically 0x10) */
    uint8_t address;
    /** I2S controller device that carries audio data */
    struct Device* i2s_device;
    /** Analog gain of all outputs in dB, from -30 to 3 */
    int32_t output_gain_db;
};

/**
 * @brief Called when the codec output becomes active or inactive.
 * Active is reported after the codec has started and its output is silent and settled, so an amplifier behind the codec
 * can be switched on without a pop. Inactive is reported before the codec powers down, so the amplifier can be switched off first.
 * Called on the task that opens or closes the stream.
 */
typedef void (*Es838xOutputCallback)(bool active, void* user_data);

/**
 * @brief Sets the callback for the output state of the codec.
 * @param[in] device an ES8388 or ES8389 device that has started, otherwise this does nothing
 * @param[in] callback the callback, or NULL to remove it
 * @param[in] user_data passed to the callback
 */
void es838x_set_output_callback(struct Device* device, Es838xOutputCallback callback, void* user_data);

#ifdef __cplusplus
}
#endif
