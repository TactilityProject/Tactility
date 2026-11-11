// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOOLBAR_ACTION_LIMIT 5

/** Configuration for toolbar-wide defaults */
typedef struct {
    /** Callback invoked when a toolbar's default navigation (top-left) button is pressed */
    lv_event_cb_t nav_action_callback;
} ToolbarConfig;

/**
 * @brief Configure toolbar-wide defaults. Must be called before any toolbar is created (e.g. during LVGL startup).
 * @param[in] config the configuration to apply
 */
void lvgl_toolbar_configure(const ToolbarConfig* config);

/**
 * @brief Create a toolbar widget with the given title.
 * @param[in] parent the parent object for the new toolbar
 * @param[in] title the toolbar title
 * @return the created toolbar
 */
lv_obj_t* lvgl_toolbar_create(lv_obj_t* parent, const char* title);

/** Sets the toolbar title */
void lvgl_toolbar_set_title(lv_obj_t* obj, const char* title);

/**
 * Sets the navigation action of the toolbar (button on the top-left)
 * @param[in] obj the toolbar instance
 * @param[in] icon the icon to set on the button
 * @param[in] callback the callback for the click action of the button
 * @param[in] userData the user data that is attached to the callback event object
 */
void lvgl_toolbar_set_nav_action(lv_obj_t* obj, const char* icon, lv_event_cb_t callback, void* userData);

/**
 * Create and add an action button with an image to the toolbar (aligned to the right of the toolbar)
 * @param[in] obj the toolbar instance
 * @param[in] imagePath the path to an image file to shown on the button
 * @param[in] callback the callback for the click action of the button
 * @param[in] callbackUserData the user data that is passed to the callback
 * @return an lv_button instance
 */
lv_obj_t* lvgl_toolbar_add_image_button_action(lv_obj_t* obj, const char* imagePath, lv_event_cb_t callback, void* callbackUserData);

/**
 * Create and add an action button with text to the toolbar (aligned to the right of the toolbar)
 * @param[in] obj the toolbar instance
 * @param[in] text the button text
 * @param[in] callback the callback for the click action of the button
 * @param[in] callbackUserData the user data that is passed to the callback
 * @return an lv_button instance
 */
lv_obj_t* lvgl_toolbar_add_text_button_action(lv_obj_t* obj, const char* text, lv_event_cb_t callback, void* callbackUserData);

/**
 * Create and add a switch to the toolbar actions.
 * @param[in] obj the toolbar instance
 * @return an instance created by lv_switch_create()
 */
lv_obj_t* lvgl_toolbar_add_switch_action(lv_obj_t* obj);

/**
 * Create and add a dropdown to the toolbar actions.
 * @param[in] obj the toolbar instance
 * @param[in] options newline-separated dropdown options
 * @param[in] width the dropdown's width in pixels, or 0 to leave LVGL's default dropdown width
 * @param[in] text fixed text to show instead of the selected option or NULL to show the selected option as usual
 * @return an instance created by lv_dropdown_create()
 */
lv_obj_t* lvgl_toolbar_add_dropdown_action(lv_obj_t* obj, const char* options, lv_coord_t width, const char* text);

/**
 * Create and add a spinner to the toolbar actions.
 * @param[in] obj the toolbar instance
 * @return an instance created by lvgl_spinner_create()
 */
lv_obj_t* lvgl_toolbar_add_spinner_action(lv_obj_t* obj);

/**
 * Remove all actions from the toolbar
 * @param[in] obj the toolbar instance
 */
void lvgl_toolbar_clear_actions(lv_obj_t* obj);

#ifdef __cplusplus
}
#endif
