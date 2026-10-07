// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lets the arrow keys move the focus between the container's children by their position, e.g. in a grid of buttons.
 * The container takes the place of its children in the focus group. Moving past the first or last row leaves the container.
 * @param[in] container the container with the focusable children
 */
void lvgl_grid_navigation_add(lv_obj_t* container);

/**
 * @brief Turns grid navigation off again: the focusable children rejoin the container's focus group.
 * @param[in] container a container that was passed to lvgl_grid_navigation_add()
 */
void lvgl_grid_navigation_remove(lv_obj_t* container);

/**
 * @brief Moves the focus to the next or previous child in their order, for devices that can only step through widgets.
 * Past the last or first child, the focus moves to the next or previous object in the container's focus group.
 * @param[in] container a container that was passed to lvgl_grid_navigation_add()
 * @param[in] next true for the next child, false for the previous child
 */
void lvgl_grid_navigation_focus_next(lv_obj_t* container, bool next);

/**
 * @param[in] container a container that was passed to lvgl_grid_navigation_add()
 * @return the focused child, or NULL when no child is focused
 */
lv_obj_t* lvgl_grid_navigation_get_focused(lv_obj_t* container);

/**
 * @param[in] obj the object to check
 * @return true when the object is a container that was passed to lvgl_grid_navigation_add()
 */
bool lvgl_grid_navigation_is_container(const lv_obj_t* obj);

#ifdef __cplusplus
}
#endif
