#pragma once

#include "../app/AppContext.h"

#include <lvgl.h>

namespace tt::lvgl {

#define TOOLBAR_ACTION_LIMIT 4

/** Create a toolbar widget that shows the app name as title */
lv_obj_t* toolbar_create(lv_obj_t* parent, const app::AppContext& app);

/** Create a toolbar widget with the provided title*/
lv_obj_t* toolbar_create(lv_obj_t* parent, const std::string& title);

/** Sets the toolbar title */
void toolbar_set_title(lv_obj_t* obj, const std::string& title);

/** Sets the navigation action of the toolbar (button on the top-left)
 * @param[in] obj the toolbar instance
 * @param[in] icon the icon to set on the button
 * @param[in] callback the callback for the click action of the button
 * @param[in] callbackEventUserData the user data that is attached to the callback event object
 */
void toolbar_set_nav_action(lv_obj_t* obj, const char* icon, lv_event_cb_t callback, void* userData);

/**
 * Create and add an action button with an image to the toolbar (aligned to the right of the toolbar)
 * @param[in] obj the toolbar instance
 * @param[in] imagePath the path to an image file to shown on the button
 * @param[in] callback the callback for the click action of the button
 * @param[in] callbackUserData the user data that is passed to the callback
 * @return an lv_button instance
 */
lv_obj_t* toolbar_add_image_button_action(lv_obj_t* obj, const char* imagePath, lv_event_cb_t callback, void* callbackUserData);

/**
 * Create and add an action button with text to the toolbar (aligned to the right of the toolbar)
 * @param[in] obj the toolbar instance
 * @param[in] text the button text
 * @param[in] callback the callback for the click action of the button
 * @param[in] callbackUserData the user data that is passed to the callback
 * @return an lv_button instance
 */
lv_obj_t* toolbar_add_text_button_action(lv_obj_t* obj, const char* text, lv_event_cb_t callback, void* callbackUserData);

/**
 * Create and add a switch to the toolbar actions.
 * @param[in] obj the toolbar instance
 * @return an instance created by lv_switch_create()
 */
lv_obj_t* toolbar_add_switch_action(lv_obj_t* obj);

/**
 * Create and add a spinner to the toolbar actions.
 * @param[in] obj the toolbar instance
 * @return an instance created by Tactility's spinner_create()
 */
lv_obj_t* toolbar_add_spinner_action(lv_obj_t* obj);

/**
 * Remove all actions from the toolbar
 * @param[in] obj the toolbar instance
 */
void toolbar_clear_actions(lv_obj_t* obj);

} // namespace
