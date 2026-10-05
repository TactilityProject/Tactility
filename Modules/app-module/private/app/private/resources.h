// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/instance.h>

#include <tactility/error.h>

#include <stdint.h>

/** How long an app's own tasks get to end by themselves after its main task ended, before they're deleted. */
constexpr uint32_t APP_CLEANUP_TASK_GRACE_MS = 1000;

/** Starts tracking the resources of @a app_instance_id (see app/resources.h). */
error_t app_resources_register(AppInstanceId app_instance_id);

/**
 * First phase of releasing what @a app_instance_id left behind: its tasks are deleted after APP_CLEANUP_TASK_GRACE_MS,
 * and no new ones can start. Must run before the app's binary is unloaded, as they run its code.
 * Until app_resources_release(), the calling task's own calls are tracked for the instance,
 * so what the binary's static destructors free or close while it's unloaded is no longer tracked.
 * Called by the instance's own task, after it left the instance.
 */
void app_resources_release_tasks(AppInstanceId app_instance_id);

/**
 * Second phase: stops tracking @a app_instance_id, closes its files, directories and fds and frees its memory.
 * Must run after the app's binary is unloaded, on the task that called app_resources_release_tasks().
 */
void app_resources_release(AppInstanceId app_instance_id);
