// SPDX-License-Identifier: Apache-2.0
#include "gdep103tc2_gray.h"

static constexpr int BITS_PER_NIBBLE = 4;
static constexpr uint8_t NIBBLE_MASK = 0x0F;
static constexpr int BITS_PER_PIXEL_CODE = 2;

error_t gdep103tc2_gray_build_luts(uint8_t* luts, const uint8_t* waveform, int passes) {
    if (waveform == nullptr || passes <= 0) return ERROR_INVALID_ARGUMENT;

    for (int pass = 0; pass < passes; pass++) {
        uint8_t* lut = luts + pass * GRAY_LUT_SIZE;
        for (size_t pair = 0; pair < GRAY_LUT_SIZE; pair++) {
            const uint8_t left = waveform[(pair >> BITS_PER_NIBBLE) * passes + pass];
            const uint8_t right = waveform[(pair & NIBBLE_MASK) * passes + pass];
            if (left > 2 || right > 2) return ERROR_INVALID_ARGUMENT;
            lut[pair] = static_cast<uint8_t>(left | (right << BITS_PER_PIXEL_CODE));
        }
    }
    return ERROR_NONE;
}
