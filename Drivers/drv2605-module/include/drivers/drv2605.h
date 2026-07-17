// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

struct Drv2605Config {
    // Devicetree address hint (0x5A).
    uint8_t address;
    bool autoplay;
    // WaveSequence1.. effect slots to play once at start-up when autoplay is set. NULL/0-length
    // falls back to a 3x strong-click "buzz" ({1,1,1}).
    const uint8_t* startup_waveform;
    uint32_t startup_waveform_length;
};

#ifdef __cplusplus
}
#endif
