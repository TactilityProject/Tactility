// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <tactility/drivers/gpio.h>

/** Waveform/refresh mode used for automatic full-screen refreshes. */
enum Gdeq031t10RefreshMode {
    GDEQ031T10_REFRESH_FULL = 0, // ~3s, best quality
    GDEQ031T10_REFRESH_FAST = 1, // ~1.0s
    GDEQ031T10_REFRESH_SLOW = 2, // ~1.5s, fast LUT with extra settling
    GDEQ031T10_REFRESH_PARTIAL = 3 // ~0.5s, full-frame partial-mode refresh (more ghosting)
};

struct Gdeq031t10Config {
    struct GpioPinSpec pin_dc;
    struct GpioPinSpec pin_reset;
    struct GpioPinSpec pin_busy;
    int clock_speed_hz;
    enum Gdeq031t10RefreshMode refresh_mode;
    /** Panel is mounted upside down relative to the reference orientation */
    bool mirror_180;
};

#ifdef __cplusplus
}
#endif
