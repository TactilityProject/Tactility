// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/instance.h>
#include <lvgl.h>
#include <tactility/Memory.hpp>
#include <tactility/error.h>

#include <memory>

/**
 * A dialog that builds its own widgets. It behaves like the other dialogs: one at a time, over the window of the app,
 * with its own focus group and task.
 */
class DialogContent {
public:
    virtual ~DialogContent() = default;

    /**
     * Builds the widgets in a card, which is a flex column. Called on the dialog task with the LVGL lock held.
     * @param[in] handle for dialog_content_post_work() and dialog_content_close_locked()
     * @param[in] group the focus group of the dialog
     */
    virtual void build(void* handle, lv_obj_t* card, lv_group_t* group) = 0;

    /** The dialog closed: forget the widgets, which are deleted next. Called with the LVGL lock held. */
    virtual void detach() = 0;

    /** Runs on the dialog task without the LVGL lock after dialog_content_post_work(), e.g. to access files */
    virtual void work() = 0;

    /** Runs on the dialog task after the widgets were deleted: waits for what could still use the content */
    virtual void release() = 0;

    /** Reports the result. Not called when the window of the app is removed. */
    virtual void deliver(bool confirmed) = 0;
};

/** For content that was created with tt::newObject() */
struct DialogContentDeleter {
    void operator()(DialogContent* content) const { tt::deleteObject(content); }
};

using DialogContentPtr = std::unique_ptr<DialogContent, DialogContentDeleter>;

/** Shows a dialog with the content, which is freed when the dialog is gone or on failure. Errors like the other dialogs. */
error_t dialog_start_content(AppInstanceId app_instance_id, DialogContentPtr content);

/** Makes the dialog task call DialogContent::work() */
void dialog_content_post_work(void* handle);

/** @warning Caller must hold the LVGL lock */
void dialog_content_close_locked(void* handle, bool confirmed);

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
