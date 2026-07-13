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
 * @brief Dummy class-D amplifier configuration (set from devicetree).
 *
 * Covers any speaker amp with no I2C/register interface -- just I2S data in plus an
 * optional GPIO enable (SD) pin -- e.g. MAX98357A, NS4168. There is no I2C parent; the
 * device sits standalone in the devicetree and only references the I2S controller by name.
 */
struct DummyI2sAmpConfig {
    /** Name of the I2S controller device that carries audio data (e.g. "i2s0") */
    const char* i2s_device_name;
    /** Optional amplifier enable pin (SD pin). GPIO_PIN_SPEC_NONE if not wired. */
    struct GpioPinSpec enable_pin;
};

#ifdef __cplusplus
}
#endif
