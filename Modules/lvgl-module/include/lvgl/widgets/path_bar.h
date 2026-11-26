// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Called when a segment of a path bar was pressed.
 * @param[in] bar the path bar
 * @param[in] path the directory of the segment
 * @param[in] from_keys whether keys pressed it, so the selection can continue on the path bar
 * @param[in] user_data the value that was passed when the path bar was created
 */
typedef void (*LvglPathBarPressedFn)(lv_obj_t* bar, const char* path, bool from_keys, void* user_data);

/**
 * Called after the segments were rebuilt, e.g. to replace references to deleted segments.
 * @param[in] bar the path bar
 * @param[in] user_data the value that was passed when the path bar was created
 */
typedef void (*LvglPathBarRebuiltFn)(lv_obj_t* bar, void* user_data);

/**
 * @brief Adds a path bar to a toolbar, in the place of its title: a button per directory of the path, from the root to the current
 * directory, which is shown as text and can't be pressed. The toolbar's title should be empty.
 * When the path doesn't fit, the leftmost segments are replaced by "..", and then the name of the current directory is shortened.
 * The segments are buttons of the toolbar itself, so the arrow keys move between them and the toolbar's other buttons.
 * They are rebuilt when the toolbar changes size.
 * @param[in] toolbar a toolbar created with lvgl_toolbar_create()
 * @param[in] on_pressed called when a segment is pressed, may be NULL
 * @param[in] on_rebuilt called after the segments were rebuilt, may be NULL
 * @param[in] user_data passed to the callbacks
 * @return the path bar: a hidden object in the toolbar that holds its state. It is deleted with the toolbar.
 */
lv_obj_t* lvgl_path_bar_create(lv_obj_t* toolbar, LvglPathBarPressedFn on_pressed, LvglPathBarRebuiltFn on_rebuilt, void* user_data);

/**
 * @brief Shows a path. The segments are rebuilt.
 * @param[in] bar the path bar
 * @param[in] path an absolute path, which is copied
 */
void lvgl_path_bar_set_path(lv_obj_t* bar, const char* path);

/**
 * @return the segment of the parent of the current directory, or the current directory when its parent isn't shown.
 * NULL before a path was set. Valid until the segments are rebuilt.
 */
lv_obj_t* lvgl_path_bar_get_parent_segment(lv_obj_t* bar);

/** @brief Selects the segment of the parent of the current directory in the toolbar's grid navigation. */
void lvgl_path_bar_focus_parent_segment(lv_obj_t* bar);

#ifdef __cplusplus
}
#endif
