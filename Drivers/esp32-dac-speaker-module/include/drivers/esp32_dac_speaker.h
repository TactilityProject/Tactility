// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/drivers/gpio.h>

/**
 * @brief ESP32 internal-DAC speaker configuration.
 *
 * Routes 8-bit mono PCM to an ESP32 DAC channel behind an external analog amplifier.
 * The DAC channel is derived from the pin number (ESP32: GPIO25 = DAC1, GPIO26 = DAC2).
 */
struct Esp32DacSpeakerConfig {
    /** DAC output pin. Must map to a DAC channel; anything else fails to start. */
    struct GpioPinSpec pin;
    /** Native sample rate the DAC is driven at in Hz (8-bit data). */
    uint32_t sample_rate;
};

#ifdef __cplusplus
}
#endif