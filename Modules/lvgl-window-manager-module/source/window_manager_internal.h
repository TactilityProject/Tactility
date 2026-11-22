// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/instance.h>
#include <lvgl.h>

/**
 * @param[in] app_instance_id the app
 * @return the widget of the window of the app, or NULL when the app has no topmost window
 * @warning Caller must hold the LVGL lock
 */
lv_obj_t* window_manager_get_app_widget_locked(AppInstanceId app_instance_id);

/**
 * @return the container that the windows are placed in, which is the area that an app can use, or NULL when the window manager isn't started
 * @warning Caller must hold the LVGL lock
 */
lv_obj_t* window_manager_get_content_widget_locked();

/**
 * Marks the dialogs of the app as canceled, so they don't call their result function.
 * Called before the widgets of a window of the app are deleted.
 */
void dialog_cancel_for_app(AppInstanceId app_instance_id);

/**
 * Waits until the dialog tasks of the app have finished, for a limited time. Call it without holding the LVGL lock.
 * Called after the widgets of a window of the app were deleted.
 */
void dialog_wait_for_app(AppInstanceId app_instance_id);

/** Frees the tasks of dialogs that have finished */
void dialog_reap_tasks();

/**
 * @return the focus group of the dialog that is shown, or NULL
 * @warning Caller must hold the LVGL lock
 */
lv_group_t* dialog_get_group_locked();
