// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/instance.h>

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Resource tracking for apps whose manifest sets APP_MANIFEST_FLAG_CLEANUP.
 * The platform's hooks for an app binary's own calls report what it acquires and releases here.
 * Every function does nothing when the calling task's app instance doesn't track its resources.
 */

/** Ends a task that was still running when its app's main task ended. */
typedef void (*AppResourceTaskDelete)(void* handle);

/** Waits for a task that AppResourceTaskDelete only asked to end, so it no longer runs app code. */
typedef void (*AppResourceTaskJoin)(void* handle);

/**
 * Creates a task.
 * @param[in] context as passed to app_resources_create_task()
 * @param[out] out_handle identifies the task, also when untracking it
 * @return true on success
 */
typedef bool (*AppResourceTaskCreate)(void* context, void** out_handle);

/** @return the calling task's app instance if it tracks its resources, 0 otherwise */
AppInstanceId app_resources_current_app(void);

/**
 * Makes the calling task part of @a app_instance_id, so its own calls are tracked for that instance too.
 * Called first thing by a task an app created, with the id from app_resources_current_app() of the creating task.
 * @return false when @a app_instance_id no longer tracks its resources: its binary may be unloaded, so the task must end without running app code
 */
bool app_resources_enter_task(AppInstanceId app_instance_id);

/**
 * Like app_resources_enter_task(), but the calling thread's calls are only tracked for @a app_instance_id:
 * it doesn't become part of the instance otherwise. For a thread that isn't a FreeRTOS task.
 */
bool app_resources_enter_thread(AppInstanceId app_instance_id);

void app_resources_track_alloc(void* ptr);
void app_resources_untrack_alloc(void* ptr);

void app_resources_track_fd(int fd);
void app_resources_untrack_fd(int fd);

void app_resources_track_file(FILE* file);
void app_resources_untrack_file(FILE* file);

void app_resources_track_dir(DIR* dir);
void app_resources_untrack_dir(DIR* dir);

/**
 * Calls @a create and tracks the task it created, before that task can untrack itself.
 * @param[in] delete_task ends the task if it's still running after the grace period
 * @param[in] join_task nullable, called after @a delete_task, when the task deletion is asynchronous
 * @return the result of @a create
 */
bool app_resources_create_task(AppResourceTaskCreate create, void* context, AppResourceTaskDelete delete_task, AppResourceTaskJoin join_task);

/** Called by a task that is ending by itself, or by whoever ends it. */
void app_resources_untrack_task(void* handle);

#ifdef __cplusplus
}
#endif
