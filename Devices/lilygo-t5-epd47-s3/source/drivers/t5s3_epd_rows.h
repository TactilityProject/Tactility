// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Conversion of framebuffer rows into the data the source driver of the ED047TC1 panel expects.
// This code has no hardware access.

#pragma once

#include "t5s3_epd_waveform.h"

#include <cstdint>

#ifdef ESP_PLATFORM
#include <esp_attr.h>
#define T5S3_EPD_IRAM IRAM_ATTR
#else
#define T5S3_EPD_IRAM
#endif

constexpr int T5S3_EPD_WIDTH = 960;
constexpr int T5S3_EPD_HEIGHT = 540;

// Framebuffer rows hold 4 bits per pixel with the even pixel of a pair in the low nibble, 0x0 is black and 0xF is white
constexpr int T5S3_EPD_FB_ROW_BYTES = T5S3_EPD_WIDTH / 2;
constexpr int T5S3_EPD_FB_BYTES = T5S3_EPD_FB_ROW_BYTES * T5S3_EPD_HEIGHT;

// Rows on the bus hold 2 bits per pixel with the first of four pixels in the lowest bits
constexpr int T5S3_EPD_BUS_ROW_BYTES = T5S3_EPD_WIDTH / 4;

constexpr uint8_t T5S3_EPD_ACTION_HOLD = 0;
constexpr uint8_t T5S3_EPD_ACTION_DARK = 1;
constexpr uint8_t T5S3_EPD_ACTION_LIGHT = 2;

// Tables map (target level << 4 | source level) to the action for one phase of an update.

// Fast update: pixels that end up black are driven dark and pixels that end up white are driven light
inline void t5s3_epd_build_fast_table(uint8_t* table) {
    for (int to = 0; to < 16; to++) {
        for (int from = 0; from < 16; from++) {
            uint8_t action = T5S3_EPD_ACTION_HOLD;
            if (to == 0 && from != 0) {
                action = T5S3_EPD_ACTION_DARK;
            } else if (to == 15 && from != 15) {
                action = T5S3_EPD_ACTION_LIGHT;
            }
            table[(to << 4) | from] = action;
        }
    }
}

// The waveform drives white pixels that stay white with a dark and a light pulse, which flashes them.
// Holding them instead leaves the rest of an area alone, so an update of a part of the screen does not leave visible columns.
inline void t5s3_epd_build_quality_table(uint8_t* table, int phase, bool flash_white) {
    const uint8_t* packed = T5S3_EPD_QUALITY_LUT[phase];
    for (int to = 0; to < 16; to++) {
        for (int group = 0; group < 4; group++) {
            const uint8_t actions = *packed++;
            const int index = (to << 4) | (group * 4);
            table[index] = (actions >> 6) & 3;
            table[index + 1] = (actions >> 4) & 3;
            table[index + 2] = (actions >> 2) & 3;
            table[index + 3] = actions & 3;
        }
    }
    if (!flash_white) {
        table[(15 << 4) | 15] = T5S3_EPD_ACTION_HOLD;
    }
}

// Keeps the pixels of changed columns and holds all others. A framebuffer byte of the changes holds the XOR of two pixels.
inline void t5s3_epd_build_line_mask(uint8_t* mask, const uint8_t* changes) {
    for (int i = 0; i < T5S3_EPD_BUS_ROW_BYTES; i++) {
        const uint8_t first = changes[2 * i];
        const uint8_t second = changes[2 * i + 1];
        uint8_t value = 0;
        value |= (first & 0x0F) != 0 ? 0x03 : 0x00;
        value |= (first & 0xF0) != 0 ? 0x0C : 0x00;
        value |= (second & 0x0F) != 0 ? 0x30 : 0x00;
        value |= (second & 0xF0) != 0 ? 0xC0 : 0x00;
        mask[i] = value;
    }
}

// Converts one row of target levels and one row of source levels into bus data.
// All pointers must be 4-byte aligned. Four framebuffer bytes (8 pixels) become two bus bytes per step.
inline void T5S3_EPD_IRAM t5s3_epd_prepare_row(
    uint8_t* out,
    const uint8_t* to_row,
    const uint8_t* from_row,
    const uint8_t* table,
    const uint8_t* mask
) {
    const auto* to_words = reinterpret_cast<const uint32_t*>(to_row);
    const auto* from_words = reinterpret_cast<const uint32_t*>(from_row);
    const auto* mask_words = reinterpret_cast<const uint16_t*>(mask);
    auto* out_words = reinterpret_cast<uint16_t*>(out);
    for (int i = 0; i < T5S3_EPD_FB_ROW_BYTES / 4; i++) {
        const uint32_t to = to_words[i];
        const uint32_t from = from_words[i];
        // Table index of a pixel is (target level << 4) | source level, the even pixel of a byte is in its low nibble
        const uint32_t even = ((to & 0x0F0F0F0F) << 4) | (from & 0x0F0F0F0F);
        const uint32_t odd = (to & 0xF0F0F0F0) | ((from >> 4) & 0x0F0F0F0F);
        const uint32_t first = table[even & 0xFF] | (table[odd & 0xFF] << 2) | (table[(even >> 8) & 0xFF] << 4) | (table[(odd >> 8) & 0xFF] << 6);
        const uint32_t second = table[(even >> 16) & 0xFF] | (table[(odd >> 16) & 0xFF] << 2) | (table[even >> 24] << 4) | (table[odd >> 24] << 6);
        out_words[i] = static_cast<uint16_t>((first | (second << 8)) & mask_words[i]);
    }
}
