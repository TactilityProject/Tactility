// SPDX-License-Identifier: Apache-2.0
#include <binfont/render.h>

#include <tactility/memory.h>

namespace {

constexpr size_t STACK_BITMAP_SIZE = 1024;

uint16_t blend_rgb565(uint16_t foreground, uint16_t background, uint8_t alpha) {
    const uint32_t inverse = 255U - alpha;
    const uint32_t r = (((foreground >> 11) & 0x1FU) * alpha + ((background >> 11) & 0x1FU) * inverse + 127U) / 255U;
    const uint32_t g = (((foreground >> 5) & 0x3FU) * alpha + ((background >> 5) & 0x3FU) * inverse + 127U) / 255U;
    const uint32_t b = ((foreground & 0x1FU) * alpha + (background & 0x1FU) * inverse + 127U) / 255U;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

void draw_glyph(PixelBuffer* buffer, int x, int baselineY, BinFont* font, const BinFontGlyph& glyph, uint16_t color, PixelBufferConversion conversion) {
    const size_t pixel_count = static_cast<size_t>(glyph.width) * glyph.height;
    if (pixel_count == 0) {
        return;
    }

    uint8_t stack_bitmap[STACK_BITMAP_SIZE];
    uint8_t* allocated = nullptr;
    uint8_t* bitmap = stack_bitmap;
    if (pixel_count > sizeof(stack_bitmap)) {
        allocated = static_cast<uint8_t*>(memory_alloc(pixel_count));
        if (allocated == nullptr) {
            return;
        }
        bitmap = allocated;
    }

    if (binfont_get_glyph_bitmap(font, &glyph, bitmap, glyph.width) == ERROR_NONE) {
        const int left = x + glyph.x;
        const int top = baselineY - glyph.y - glyph.height;
        for (int row = 0; row < glyph.height; row++) {
            const uint8_t* line = bitmap + row * glyph.width;
            for (int column = 0; column < glyph.width; column++) {
                const uint8_t alpha = line[column];
                if (alpha == 0) {
                    continue;
                }
                const int pixel_x = left + column;
                const int pixel_y = top + row;
                const uint16_t pixel = (alpha == 255) ? color : blend_rgb565(color, pixel_buffer_get_pixel_rgb565(buffer, pixel_x, pixel_y), alpha);
                pixel_buffer_set_pixel_rgb565(buffer, pixel_x, pixel_y, pixel, conversion);
            }
        }
    }

    memory_free(allocated);
}

/** Decodes one UTF-8 codepoint and advances text. Invalid sequences decode as U+FFFD. */
uint32_t next_codepoint(const char*& text) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(text);
    const uint8_t lead = bytes[0];
    int length;
    uint32_t codepoint;
    if (lead < 0x80) {
        length = 1;
        codepoint = lead;
    } else if ((lead & 0xE0) == 0xC0) {
        length = 2;
        codepoint = lead & 0x1FU;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        codepoint = lead & 0x0FU;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        codepoint = lead & 0x07U;
    } else {
        text++;
        return 0xFFFD;
    }
    for (int i = 1; i < length; i++) {
        if ((bytes[i] & 0xC0) != 0x80) {
            text += i;
            return 0xFFFD;
        }
        codepoint = (codepoint << 6) | (bytes[i] & 0x3FU);
    }
    text += length;
    return codepoint;
}

} // namespace

extern "C" {

int binfont_draw_glyph_rgb565(PixelBuffer* buffer, int x, int baseline_y, BinFont* font, uint32_t codepoint, uint16_t color, PixelBufferConversion conversion) {
    BinFontGlyph glyph;
    if (!binfont_get_glyph(font, codepoint, &glyph)) {
        return 0;
    }
    draw_glyph(buffer, x, baseline_y, font, glyph, color, conversion);
    return static_cast<int>((glyph.advance_x16 + 8) >> 4);
}

int binfont_draw_text_rgb565(PixelBuffer* buffer, int x, int baseline_y, BinFont* font, const char* text, uint16_t color, PixelBufferConversion conversion) {
    // When the file can't be kept open, every glyph lookup opens it by itself
    const bool session = binfont_begin(font) == ERROR_NONE;
    int32_t pen_x16 = 0;
    uint32_t previous_glyph_id = 0;
    while (*text != '\0') {
        BinFontGlyph glyph;
        if (!binfont_get_glyph(font, next_codepoint(text), &glyph)) {
            previous_glyph_id = 0;
            continue;
        }
        if (previous_glyph_id != 0) {
            pen_x16 += binfont_get_kerning_x16(font, previous_glyph_id, glyph.glyph_id);
        }
        draw_glyph(buffer, x + ((pen_x16 + 8) >> 4), baseline_y, font, glyph, color, conversion);
        pen_x16 += static_cast<int32_t>(glyph.advance_x16);
        previous_glyph_id = glyph.glyph_id;
    }
    if (session) {
        binfont_end(font);
    }
    return (pen_x16 + 8) >> 4;
}

}
