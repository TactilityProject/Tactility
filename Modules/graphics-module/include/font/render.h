// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <font/font.h>
#include <graphics/pixel_buffer.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Renders one character's glyph into a PixelBuffer, in whatever pixel format it holds.
 *
 * The glyph occupies a font->glyph_width x font->glyph_height box at (x, y). A character outside
 * [first_codepoint, last_codepoint] renders as a space.
 *
 * @param[out] buffer destination, in any pixel format
 * @param[in] x pixel x of the glyph's top-left corner
 * @param[in] y pixel y of the glyph's top-left corner
 * @param[in] font the font to render with
 * @param[in] character the character to render
 * @param[in] fgColor colour for covered glyph pixels, as RGB565
 * @param[in] bgColor colour for uncovered glyph pixels, as RGB565
 * @param[in] conversion how fg/bg reduce into buffer's native format where that's ambiguous (see
 * PixelBufferConversion)
 */
void font_render_char_pixel_buffer_rgb565(
    struct PixelBuffer* buffer,
    int x,
    int y,
    const FixedWidthFont* font,
    char character,
    uint16_t fgColor,
    uint16_t bgColor,
    enum PixelBufferConversion conversion
);

#ifdef __cplusplus
}
#endif
