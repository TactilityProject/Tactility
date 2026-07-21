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
 * @brief AW88298 codec device configuration.
 *
 * The AW88298 is an output-only (speaker power amp) codec. The I2C bus is the
 * device's parent (per i2c-device.yaml), while the I2S controller carrying the
 * audio data is referenced via a devicetree phandle.
 *
 * The chip's I2C interface does not respond until its hardware RESET (RSTN)
 * line is released, so if that pin is wired (e.g. behind a GPIO expander on
 * M5Stack StackChan/CoreS3), this driver asserts and releases it itself before
 * opening the codec. This guarantees correct ordering: devicetree GPIO
 * controller devices (e.g. the expander driving reset_pin) start in
 * declaration order before this device, whereas board Configuration.cpp
 * initBoot() callbacks run after ALL devicetree devices have already started
 * and so cannot reliably gate a devicetree device's own bring-up.
 */
struct Aw88298Config {
    /** I2C address on the bus (typically 0x36) */
    uint8_t address;
    /** I2S controller device that carries audio data */
    struct Device* i2s_device;
    /** Optional hardware reset (RSTN) pin. GPIO_PIN_SPEC_NONE if tied high in hardware. */
    struct GpioPinSpec reset_pin;
};

#ifdef __cplusplus
}
#endif
