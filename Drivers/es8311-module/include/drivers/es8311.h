// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <tactility/error.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/drivers/gpio.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ES8311 codec device configuration.
 *
 * The ES8311 is wired as both control (I2C) and audio (I2S) device: the I2C
 * bus is the device's parent (per i2c-device.yaml), while the I2S controller
 * is referenced via a devicetree phandle. It operates in dual mode
 * (speaker output + mic input) on boards such as M5Stack StickS3.
 */
struct Es8311Config {
    /** I2C address on the bus (typically 0x18) */
    uint8_t address;
    /** I2S controller device that carries audio data */
    struct Device* i2s_device;
    /**
     * Optional speaker amplifier enable pin. When set, the codec driver drives it through
     * esp_codec_dev as the PA control line.
     */
    struct GpioPinSpec pin_pa;
    /** Whether the PA enable line is active-low. */
    bool pa_inverted;
    /** Disable the DAC reference (required by some board layouts, e.g. ESP-Mosaico). */
    bool no_dac_ref;
};

#ifdef __cplusplus
}
#endif
