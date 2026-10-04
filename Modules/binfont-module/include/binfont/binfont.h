// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A bitmap font in the lv_font_conv binary format (lv_font_conv --format bin).
 * Thread-safe: a font can be used from multiple tasks at the same time.
 */
struct BinFont;

typedef struct BinFontMetrics {
    /** Font size in pixels, as passed to the converter */
    uint16_t size;
    /** Pixels above the baseline */
    int16_t ascent;
    /** Pixels below the baseline (negative) */
    int16_t descent;
    /** ascent - descent */
    uint16_t line_height;
    /** Distance from the bottom of the line to the baseline (-descent) */
    int16_t base_line;
    int16_t underline_position;
    uint16_t underline_thickness;
    /** Bits per pixel of the stored bitmaps (1-4) */
    uint8_t bpp;
} BinFontMetrics;

typedef struct BinFontGlyph {
    uint32_t glyph_id;
    /** Horizontal advance in 1/16th pixels */
    uint32_t advance_x16;
    /** Bounding box left, relative to the pen position */
    int16_t x;
    /** Bounding box bottom, relative to the baseline (positive is up) */
    int16_t y;
    uint16_t width;
    uint16_t height;
} BinFontGlyph;

/**
 * @brief Opens a font file.
 * The whole file is loaded into external memory (PSRAM) when available, otherwise into internal memory.
 * When that allocation fails, only the lookup tables are loaded and glyph data is read from the file
 * on demand: the file is opened and closed for every glyph lookup, unless it's kept open with
 * binfont_begin()/binfont_end().
 * @param[in] path the file path
 * @param[out] out the opened font
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND when the file can't be opened
 * @retval ERROR_NOT_SUPPORTED when the file is not a valid or supported font
 * @retval ERROR_OUT_OF_MEMORY when memory allocation failed
 */
error_t binfont_open_file(const char* path, struct BinFont** out);

/**
 * @brief Opens a font from memory.
 * @param[in] data the font file contents, which must stay valid until binfont_close()
 * @param[in] size the size of data in bytes
 * @param[in] take_ownership when true, data is released with memory_free() by binfont_close() (also on failure)
 * @param[out] out the opened font
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_SUPPORTED when the data is not a valid or supported font
 * @retval ERROR_OUT_OF_MEMORY when memory allocation failed
 */
error_t binfont_open_memory(const void* data, size_t size, bool take_ownership, struct BinFont** out);

/** Releases the font and all its resources. */
void binfont_close(struct BinFont* font);

/**
 * @brief Keeps the font file open until the matching binfont_end(), so rendering many glyphs
 * doesn't open and close the file for each of them. Calls can be nested and used from multiple tasks.
 * Does nothing for fonts that are fully loaded in memory.
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND when the file can't be opened
 */
error_t binfont_begin(struct BinFont* font);

/** Ends a binfont_begin() session. The file is closed when no sessions remain. */
void binfont_end(struct BinFont* font);

void binfont_get_metrics(const struct BinFont* font, BinFontMetrics* out);

/**
 * @param[in] font the font
 * @param[in] codepoint the Unicode codepoint
 * @param[out] out the glyph info
 * @return false when the font has no glyph for this codepoint
 */
bool binfont_get_glyph(struct BinFont* font, uint32_t codepoint, BinFontGlyph* out);

/** @return the kerning adjustment for a glyph pair in 1/16th pixels, or 0 when there is none */
int32_t binfont_get_kerning_x16(struct BinFont* font, uint32_t left_glyph_id, uint32_t right_glyph_id);

/**
 * @brief Decodes a glyph bitmap as 8-bit alpha (0 = transparent, 255 = opaque).
 * @param[in] font the font
 * @param[in] glyph a glyph from binfont_get_glyph()
 * @param[out] out the destination: glyph->height rows of glyph->width bytes
 * @param[in] stride bytes per row in out (at least glyph->width)
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT when stride is too small
 * @retval ERROR_RESOURCE when the glyph data can't be read
 */
error_t binfont_get_glyph_bitmap(struct BinFont* font, const BinFontGlyph* glyph, uint8_t* out, size_t stride);

#ifdef __cplusplus
}
#endif
