// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

#define GDEP103TC2_DATA_PIN_COUNT 16
#define GDEP103TC2_GRAY_LEVELS 16

struct Gdep103tc2Config {
    /** Clockwise rotation in degrees: 0, 90, 180 or 270. 0 is the panel's originall 1872x1404 landscape. */
    uint32_t rotation;
    /** 16 shades with a waveform instead of black and white. Updates take about a second. */
    bool grayscale;
    /** Grayscale waveform */
    const uint8_t* gray_waveform;
    /** Number of elements in gray_waveform. A multiple of GDEP103TC2_GRAY_LEVELS. */
    size_t gray_waveform_length;
    /** Clock of the parallel bus in hertz */
    uint32_t pixel_clock_hz;
    /** Optional board-level supply enable. Omitted when the board keeps the panel rails on. */
    struct GpioPinSpec pin_power_enable;
    /** Source driver output enable */
    struct GpioPinSpec pin_oe;
    /** Gate driver mode select, high while the panel is driven */
    struct GpioPinSpec pin_gmod;
    /** Write clock of the parallel bus. Must be an ESP32-S3 GPIO. */
    struct GpioPinSpec pin_wr;
    /** Source driver start pulse */
    struct GpioPinSpec pin_sph;
    /** Gate driver clock. Must be an ESP32-S3 GPIO. */
    struct GpioPinSpec pin_ckv;
    /** Gate driver start pulse. Must be an ESP32-S3 GPIO. */
    struct GpioPinSpec pin_spv;
    /** Source driver latch enable. Must be an ESP32-S3 GPIO. */
    struct GpioPinSpec pin_le;
    /** Bits of the parallel data bus. Must be ESP32-S3 GPIOs. */
    struct GpioPinSpec pin_data0;
    struct GpioPinSpec pin_data1;
    struct GpioPinSpec pin_data2;
    struct GpioPinSpec pin_data3;
    struct GpioPinSpec pin_data4;
    struct GpioPinSpec pin_data5;
    struct GpioPinSpec pin_data6;
    struct GpioPinSpec pin_data7;
    struct GpioPinSpec pin_data8;
    struct GpioPinSpec pin_data9;
    struct GpioPinSpec pin_data10;
    struct GpioPinSpec pin_data11;
    struct GpioPinSpec pin_data12;
    struct GpioPinSpec pin_data13;
    struct GpioPinSpec pin_data14;
    struct GpioPinSpec pin_data15;
};

#ifdef __cplusplus
}
#endif
