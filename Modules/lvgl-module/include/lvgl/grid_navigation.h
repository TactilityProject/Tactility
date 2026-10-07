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
 * @brief Moves the focus to the next or previous widget, for devices that can only step through widgets.
 * The children of grid navigation containers are visited in their order: entering a container forwards focuses its first child, and backwards its last child.
 * @param[in] group the focus group
 * @param[in] next true for the next widget, false for the previous widget
 */
void lvgl_grid_navigation_step(lv_group_t* group, bool next);

/**
 * @brief Hides the selection that keys made, e.g. when the touchscreen is used. The focus stays where it is,
 * and the next key that moves the focus shows the selection again.
 * @param[in] group the focus group
 */
void lvgl_focus_hide_key_selection(lv_group_t* group);

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
