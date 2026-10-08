// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a list: a flex column of full-width text rows and buttons.
 * @param[in] parent the parent object for the new list
 * @return the created list
 */
lv_obj_t* lvgl_list_create(lv_obj_t* parent);

/**
 * @brief Adds a full-width text row (a section header) to a list.
 * @param[in] list the list created with lvgl_list_create()
 * @param[in] text the text of the row, or NULL to leave it empty
 * @return the created label
 */
lv_obj_t* lvgl_list_add_text(lv_obj_t* list, const char* text);

/**
 * @brief Adds a full-width button with an optional icon and text to a list.
 * @param[in] list the list created with lvgl_list_create()
 * @param[in] icon an image source (e.g. a symbol), or NULL for no icon
 * @param[in] text the button text, or NULL for no text
 * @return the created button
 */
lv_obj_t* lvgl_list_add_button(lv_obj_t* list, const void* icon, const char* text);

#ifdef __cplusplus
}
#endif
