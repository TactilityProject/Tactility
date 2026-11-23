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
 * Begins removing a window of the app: marks its active dialogs as canceled and prevents new
 * dialogs from being accepted until the removal is finished.
 */
void dialog_cancel_for_app(AppInstanceId app_instance_id);

/**
 * Re-enables dialogs for an app that is creating a window, unless one of its removals is still
 * active.
 */
void dialog_allow_for_app(AppInstanceId app_instance_id);

/** Returns whether the app's dialogs are currently canceled. */
bool dialog_is_app_canceled(AppInstanceId app_instance_id);

/**
 * Finishes one removal and re-enables dialogs when no other removal is active.
 */
void dialog_finish_remove_for_app(AppInstanceId app_instance_id);

/**
 * Waits up to the drain timeout for the app's dialog tasks to finish. Returns false on timeout;
 * callers that require callback-context safety must retry until it returns true. Call without
 * holding the LVGL or lifecycle locks.
 */
bool dialog_wait_for_app(AppInstanceId app_instance_id);

/** Frees the tasks of dialogs that have finished */
void dialog_reap_tasks();

/**
 * @return the focus group of the dialog that is shown, or NULL
 * @warning Caller must hold the LVGL lock
 */
lv_group_t* dialog_get_group_locked();
