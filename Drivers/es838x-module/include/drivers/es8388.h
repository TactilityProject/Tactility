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
};

#ifdef __cplusplus
}
#endif
