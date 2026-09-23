// SPDX-License-Identifier: Apache-2.0
#include <font/render.h>

#include <cstring>

void font_render_char(const FixedWidthFont* font, char ch, uint16_t fg, uint16_t bg, uint16_t* buffer, int stride) {
    // Anything outside the font's range renders as blank rather than as garbage. The comparison
    // uses an unsigned value so that a byte above 0x7F cannot come out negative and pass the
    // lower-bound test.
    const auto raw = static_cast<unsigned char>(ch);
    const unsigned char glyphChar = (raw >= font->first_codepoint && raw <= font->last_codepoint) ? raw : ' ';
    const uint8_t* glyph = &font->glyph_bitmap[(glyphChar - font->first_codepoint) * font->glyph_height * font->glyph_bytes_per_row];

    for (int y = 0; y < font->glyph_height; y++) {
        const uint8_t* bits = &glyph[y * font->glyph_bytes_per_row];
        uint16_t* out = &buffer[y * stride];

        for (int x = 0; x < font->glyph_width; x++) {
            // The font stores the leftmost pixel of each row byte in the most significant bit.
            out[x] = (bits[x / 8] & (0x80U >> (x % 8))) ? fg : bg;
        }
    }
}

void font_render_string(const FixedWidthFont* font, const char* str, uint16_t fg, uint16_t bg, uint16_t* buffer, int stride) {
    for (const char* p = str; *p != '\0'; p++) {
        font_render_char(font, *p, fg, bg, buffer, stride);
        buffer += font->glyph_width;
    }
}

int font_get_string_length(const FixedWidthFont* font, const char* str) {
    return static_cast<int>(strlen(str)) * font->glyph_width;
}
