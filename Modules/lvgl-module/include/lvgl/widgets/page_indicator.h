// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a page indicator: a row of dots with one dot per page, where the dot of the current page is filled.
 * The theme determines the size and colour of the dots.
 * When the dots are wider than the indicator's maximum width (lv_obj_set_style_max_width()), it shows the page number instead, e.g. "2/12".
 * @param[in] parent the parent object
 * @return the created page indicator
 */
lv_obj_t* lvgl_page_indicator_create(lv_obj_t* parent);

/**
 * @brief Sets the number of pages, which is the number of dots.
 * @param[in] obj the page indicator
 * @param[in] count the number of pages
 */
void lvgl_page_indicator_set_page_count(lv_obj_t* obj, uint32_t count);

/**
 * @brief Sets the current page, whose dot is filled.
 * @param[in] obj the page indicator
 * @param[in] page the zero-based index of the current page
 */
void lvgl_page_indicator_set_page(lv_obj_t* obj, uint32_t page);

#ifdef __cplusplus
}
#endif
