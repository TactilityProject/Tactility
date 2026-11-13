// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Rasterizes SVG text into a square ARGB8888 draw buffer, scaled to fit and centered.
 * @param[in] data the SVG document
 * @param[in] length the number of bytes in @a data
 * @param[in] size width and height in pixels, from 1 to 32767
 * @return the draw buffer, to be freed with lv_draw_buf_destroy(), or NULL when the document can't be parsed or the size is out of range
 */
lv_draw_buf_t* lvgl_svg_rasterize_data(const char* data, size_t length, uint32_t size);

/**
 * Rasterizes an SVG file into a square ARGB8888 draw buffer, scaled to fit and centered.
 * Reads the file, so it must not be called on the LVGL thread.
 * @param[in] path a file system path (not an LVGL drive path)
 * @param[in] size width and height in pixels
 * @return the draw buffer, to be freed with lv_draw_buf_destroy(), or NULL when the file can't be read or parsed
 */
lv_draw_buf_t* lvgl_svg_rasterize_file(const char* path, uint32_t size);

/**
 * Rasterizes a colour or a mono variant of an icon, from SVG text.
 * The preferred variant is used when it can be parsed, otherwise the other one.
 * @param[in] colour_data the colour variant, or NULL
 * @param[in] mono_data the mono variant, or NULL
 * @param[in] prefer_mono true on a mono theme
 * @param[out] out_is_mono true when the mono variant was rasterized, so the caller can tint it with the theme colour. Can be NULL.
 * @return the draw buffer, or NULL when neither variant exists or can be parsed
 */
lv_draw_buf_t* lvgl_svg_rasterize_variants(
    const char* colour_data, size_t colour_length,
    const char* mono_data, size_t mono_length,
    bool prefer_mono, uint32_t size, bool* out_is_mono
);

/**
 * Same as lvgl_svg_rasterize_variants(), for files. Reads files, so it must not be called on the LVGL thread.
 * @param[in] colour_path the colour variant or NULL, a file system path
 * @param[in] mono_path the mono variant or NULL, a file system path
 */
lv_draw_buf_t* lvgl_svg_rasterize_file_variants(
    const char* colour_path, const char* mono_path,
    bool prefer_mono, uint32_t size, bool* out_is_mono
);

#ifdef __cplusplus
}
#endif
