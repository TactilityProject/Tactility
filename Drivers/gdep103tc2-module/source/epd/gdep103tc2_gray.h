// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "gdep103tc2_panel.h"

#include <drivers/gdep103tc2.h>

#include <tactility/error.h>

#include <esp_attr.h>

#include <stddef.h>
#include <stdint.h>

static constexpr size_t GRAY_TARGET_ROW_BYTES = PANEL_WIDTH / 2;
static constexpr size_t GRAY_TARGET_SIZE = GRAY_TARGET_ROW_BYTES * PANEL_HEIGHT;

static constexpr int GRAY_LEVELS = GDEP103TC2_GRAY_LEVELS;
static constexpr int GRAY_CLEAR_PASSES = 8;

static constexpr size_t GRAY_LUT_SIZE = 256;

/**
 * Builds one lookup table per thing from the board's row-major waveform.
 * @param[out] luts GRAY_LUT_SIZE * passes bytes
 * @param[in] waveform GRAY_LEVELS * passes codes (0 neutral, 1 darken, 2 lighten)
 */
error_t gdep103tc2_gray_build_luts(uint8_t* luts, const uint8_t* waveform, int passes);

static inline uint8_t gdep103tc2_gray_level_from_luminance(uint8_t luminance) {
    return luminance >> 4;
}

/**
 * Encodes the columns of 1 row for 1 pass
 * @note The row layout is that of gdep103tc2_encode_row()
 * @param[in] target_row 4 bits per pixel row of the target
 * @param[in] lut table of the pass
 * @param[in] x0 first column, a multiple of 8
 * @param[in] x1 end column, a multiple of 8
 * @param[out] out_row ROW_BYTES bytes
 */
static inline void IRAM_ATTR gdep103tc2_gray_encode_row(const uint8_t* target_row, const uint8_t* lut, int x0, int x1, uint8_t* out_row) {
    const int first = x0 / 8;
    const int last = x1 / 8;
    const uint8_t* pixels = target_row + x0 / 2;

    for (int group = first; group < last; group++) {
        uint8_t* out = out_row + 2 * (ROW_WORDS - 1 - group);
        out[1] = static_cast<uint8_t>(lut[pixels[0]] | (lut[pixels[1]] << 4));
        out[0] = static_cast<uint8_t>(lut[pixels[2]] | (lut[pixels[3]] << 4));
        pixels += 4;
    }
}
