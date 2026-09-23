// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/manager.h>
#include <app/start.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Shared core behind every app_start*() (app/manager.h) and app_execute*()
 * (app/execute.h) entry point: deep-copies @a context->argv, allocates an instance id, installs
 * @a context->bindings into the new instance's fd table before app_scheduler_start() is called,
 * then starts it. @a context->manifest may be NULL for a location-based start with no manifest.
 * @retval ERROR_INVALID_ARGUMENT @a context->binding_count is nonzero but @a context->bindings is NULL
 * @retval ERROR_NOT_FOUND no AppLoaderApi is registered for @a context->location.type
 * @retval ERROR_OUT_OF_RANGE a binding's producer_fd is out of range
 * @retval ERROR_RESOURCE a binding's event_group has no free bits left to claim
 * @retval ERROR_NONE on success
 */
error_t app_manager_start_internal(const struct AppStartContext* context, AppInstanceId* out_app_instance_id);

#ifdef __cplusplus
}
#endif
