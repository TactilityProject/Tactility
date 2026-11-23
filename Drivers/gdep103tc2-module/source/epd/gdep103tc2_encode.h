// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "gdep103tc2_panel.h"

#include <esp_attr.h>

#include <stddef.h>
#include <stdint.h>

static constexpr size_t TARGET_ROW_BYTES = PANEL_WIDTH / 8;
static constexpr size_t STATE_ROW_BYTES = PANEL_WIDTH / 2;
static constexpr size_t TARGET_SIZE = TARGET_ROW_BYTES * PANEL_HEIGHT;
static constexpr size_t STATE_SIZE = STATE_ROW_BYTES * PANEL_HEIGHT;

static constexpr int DRIVE_FRAMES = 3;
static_assert(DRIVE_FRAMES >= 1 && DRIVE_FRAMES <= 4, "A drive counter is done at 4 and starts at 4 - DRIVE_FRAMES");

// Pixel state with nothing is driven
static constexpr uint8_t STATE_SETTLED_WHITE = 0x90;

/**
 * Advances one pixel pair and returns the 4 bus bits for it: 
 * - the left pixel in bits 1:0 and the right one in bits 3:2.
 * @param[in,out] state_ptr state of the pair
 * @param[in] target_pair target colors of the pair, the left one is bit 1 and the right one in bit 0
 */
static inline uint8_t IRAM_ATTR gdep103tc2_drive_pair(uint8_t* state_ptr, uint8_t target_pair) {
    static constexpr uint8_t KEEP_COUNTERS[4] = { 0xFC, 0xE0, 0x1C, 0x00 };
    static constexpr uint8_t START_RIGHT = (4 - DRIVE_FRAMES) << 2;
    static constexpr uint8_t START_LEFT = (4 - DRIVE_FRAMES) << 5;
    static constexpr uint8_t START_COUNTERS[4] = { 0, START_RIGHT, START_LEFT, START_RIGHT | START_LEFT };

    uint8_t state = *state_ptr;
    const uint8_t changed = (state ^ target_pair) & 0x3;
    state = (state & KEEP_COUNTERS[changed]) | target_pair | START_COUNTERS[changed];

    uint8_t out = (state & 0x80) ? 0x0 : (target_pair & 0x2) ? 0x1 : 0x2;
    out |= (state & 0x10) ? 0x0 : (target_pair & 0x1) ? 0x4 : 0x8;

    // Increment the counters that have not reached 4
    state += ((~state) >> 2) & 0x24;
    *state_ptr = state;
    return out;
}

/**
 * Encodes the columns of one row into the bits for the bus and advances the states for pixels
 * 
 * @note the first bus word carries the rightmost 8 pixels, and within a word the bits 7:0 hold 
 * pixels 4 to 7 of the group and bits 15:8 hold pixels 0 to 3, each in reversed order
 * @param[in] target_row 1 bit per pixel row of the target
 * @param[in,out] state_row 4 bits per pixel row of the state
 * @param[in] x0 first column, a multiple of 8
 * @param[in] x1 end column, a multiple of 8
 * @param[out] out_row ROW_BYTES bytes
 */
static inline void IRAM_ATTR gdep103tc2_encode_row(const uint8_t* target_row, uint8_t* state_row, int x0, int x1, uint8_t* out_row) {
    const int first = x0 / 8;
    const int last = x1 / 8;
    uint8_t* state = state_row + x0 / 2;

    for (int group = first; group < last; group++) {
        const uint8_t pixels = target_row[group];
        uint8_t* out = out_row + 2 * (ROW_WORDS - 1 - group);
        out[1] = static_cast<uint8_t>(gdep103tc2_drive_pair(state, pixels >> 6) | (gdep103tc2_drive_pair(state + 1, (pixels >> 4) & 0x3) << 4));
        out[0] = static_cast<uint8_t>(gdep103tc2_drive_pair(state + 2, (pixels >> 2) & 0x3) | (gdep103tc2_drive_pair(state + 3, pixels & 0x3) << 4));
        state += 4;
    }
}

// 4x4 ordered (Bayer) dither
static constexpr uint8_t BAYER_4X4[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

// Thresholds scaled by 16 stay below 255, just safety
static inline bool gdep103tc2_dither_is_black(uint8_t luminance, int32_t x, int32_t y) {
    return luminance <= BAYER_4X4[y & 3][x & 3] * 16U;
}
