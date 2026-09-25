// SPDX-License-Identifier: Apache-2.0
#include <font/render.h>
#include <font/font.h>
#include <graphics/pixel_buffer.h>

extern "C" {

void font_render_char_pixel_buffer_rgb565(
    PixelBuffer* buffer,
    int x,
    int y,
    const FixedWidthFont* font,
    char character,
    uint16_t fg,
    uint16_t bg,
    PixelBufferConversion conversion
) {
    const auto raw = static_cast<unsigned char>(character);
    const unsigned char glyphChar = (raw >= font->first_codepoint && raw <= font->last_codepoint) ? raw : ' ';
    const uint8_t* glyph = &font->glyph_bitmap[(glyphChar - font->first_codepoint) * font->glyph_height * font->glyph_bytes_per_row];

    for (int row = 0; row < font->glyph_height; row++) {
        const uint8_t* bits = &glyph[row * font->glyph_bytes_per_row];

        for (int col = 0; col < font->glyph_width; col++) {
            // The font stores the leftmost pixel of each row byte in the most significant bit.
            const bool covered = (bits[col / 8] & (0x80U >> (col % 8))) != 0;
            pixel_buffer_set_pixel_rgb565(buffer, x + col, y + row, covered ? fg : bg, conversion);
        }
    }
}

}
