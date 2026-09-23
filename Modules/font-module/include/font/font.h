// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A fixed-width bitmap font: monospaced glyphs covering a contiguous codepoint range, one row of
 * glyph_bytes_per_row bytes per pixel row (bit 7 of the first byte is the leftmost pixel).
 *
 * glyph_bitmap is indexed as glyph_bitmap[(ch - first_codepoint) * glyph_height *
 * glyph_bytes_per_row + row * glyph_bytes_per_row + byte]; a codepoint outside
 * [first_codepoint, last_codepoint] has no entry.
 */
typedef struct FixedWidthFont {
    uint8_t glyph_width;
    uint8_t glyph_height;
    uint8_t glyph_bytes_per_row;
    uint8_t first_codepoint;
    uint8_t last_codepoint;
    const uint8_t* glyph_bitmap;
} FixedWidthFont;

#ifdef __cplusplus
}
#endif
