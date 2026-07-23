// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

// Waveform/refresh modes, in increasing speed / decreasing quality order.
#define GDEQ031T10_REFRESH_FULL 0
#define GDEQ031T10_REFRESH_FAST 1
#define GDEQ031T10_REFRESH_SLOW 2
#define GDEQ031T10_REFRESH_PARTIAL 3

struct Gdeq031t10Config {
    struct GpioPinSpec pin_dc;
    // Optional; see the binding's pin-reset description for what changes when this is absent.
    struct GpioPinSpec pin_reset;
    struct GpioPinSpec pin_busy;
    uint32_t clock_speed_hz;
    // One of the GDEQ031T10_REFRESH_* values above.
    uint8_t refresh_mode;
    bool mirror_180;
};

#ifdef __cplusplus
}
#endif
