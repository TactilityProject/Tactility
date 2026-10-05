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
 * Stops tracking @a app_instance_id and releases what's left: tasks are deleted after APP_CLEANUP_TASK_GRACE_MS,
 * then files, directories and fds are closed and memory is freed.
 * Must run before the app's binary is unloaded, from a task that is no longer part of the instance.
 */
void app_resources_release(AppInstanceId app_instance_id);
