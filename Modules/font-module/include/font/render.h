// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <font/font.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Renders one character's glyph into buffer, a caller-owned RGB565 pixel buffer laid out as
 * `stride` pixels per row. The glyph occupies a font->glyph_width x font->glyph_height box
 * starting at buffer's first pixel - buffer must already point at that box's top-left corner
 * within the larger destination image. A character outside [first_codepoint, last_codepoint]
 * renders as a space.
 */
void font_render_char(const FixedWidthFont* font, char ch, uint16_t fg, uint16_t bg, uint16_t* buffer, int stride);

/**
 * Renders a null-terminated string left to right, one font_render_char() call per character,
 * each advanced by font->glyph_width pixels from the last. buffer must point at the first
 * character's top-left corner; the string occupies strlen(str) * font->glyph_width x
 * font->glyph_height pixels in total.
 */
void font_render_string(const FixedWidthFont* font, const char* str, uint16_t fg, uint16_t bg, uint16_t* buffer, int stride);

/** Pixel width font_render_string() would occupy for str: strlen(str) * font->glyph_width. */
int font_get_string_length(const FixedWidthFont* font, const char* str);

#ifdef __cplusplus
}
#endif
