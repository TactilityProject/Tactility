// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <binfont/binfont.h>
#include <graphics/pixel_buffer.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Alpha-blends one glyph over the existing pixels of a PixelBuffer.
 * @param[out] buffer destination, in any pixel format
 * @param[in] x pen position x
 * @param[in] baseline_y pen position y (the baseline)
 * @param[in] font the font
 * @param[in] codepoint the Unicode codepoint
 * @param[in] color glyph colour as RGB565
 * @param[in] conversion how colours reduce into the buffer's native format (see PixelBufferConversion)
 * @return the horizontal advance in pixels, or 0 when the font has no glyph for the codepoint
 */
int binfont_draw_glyph_rgb565(
    struct PixelBuffer* buffer,
    int x,
    int baseline_y,
    struct BinFont* font,
    uint32_t codepoint,
    uint16_t color,
    enum PixelBufferConversion conversion
);

/**
 * @brief Alpha-blends a UTF-8 string over the existing pixels of a PixelBuffer, applying kerning.
 * @param[out] buffer destination, in any pixel format
 * @param[in] x pen position x
 * @param[in] baseline_y pen position y (the baseline)
 * @param[in] font the font
 * @param[in] text UTF-8 encoded, null-terminated text
 * @param[in] color glyph colour as RGB565
 * @param[in] conversion how colours reduce into the buffer's native format (see PixelBufferConversion)
 * @return the total advance in pixels
 */
int binfont_draw_text_rgb565(
    struct PixelBuffer* buffer,
    int x,
    int baseline_y,
    struct BinFont* font,
    const char* text,
    uint16_t color,
    enum PixelBufferConversion conversion
);

#ifdef __cplusplus
}
#endif
