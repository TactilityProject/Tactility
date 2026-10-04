// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

struct BinFont;

/**
 * @brief Creates an LVGL font that renders glyphs from a BinFont.
 * @param[in] font the source font, which must outlive the returned font
 * @return the LVGL font, or NULL when memory allocation failed
 */
lv_font_t* lvgl_binfont_create(struct BinFont* font);

/** Destroys a font from lvgl_binfont_create(). The BinFont is not closed. */
void lvgl_binfont_destroy(lv_font_t* font);

#ifdef __cplusplus
}
#endif
