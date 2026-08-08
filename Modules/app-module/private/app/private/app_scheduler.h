// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/manifest.h>

#include <tactility/error.h>

/**
 * Owns per-app task lifecycle on behalf of app_manager_*(). AppLoaderApi implementations
 * stay task-agnostic; all of xTaskCreate()/vTaskDelete() happens here, as a plain FreeRTOS task
 * (not TactilityKernel's Thread wrapper). Every app instance gets its own dedicated task for its
 * entire lifetime - no task is ever reused for a different instance.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Loads and starts an app instance: spawns a dedicated task that calls
 * AppLoaderApi::load()/run(), marking the instance ACTIVE for the duration of run().
 * @param[in] app_instance_id id already allocated in the ledger for this instance
 * @param[in] location the location of the app
 * @param[in] argc the amount of arguments to pass to the app's main function
 * @param[in] argv the array of arguments to pass to the app's main function - ownership is
 * taken by the scheduler regardless of outcome (freed once the spawned task's run() returns, or
 * immediately on a failure to start it)
 */
error_t app_scheduler_start(AppInstanceId app_instance_id, struct AppLocation location, int argc, char* argv[]);

/**
 * Permanently stops an app instance (APP_EVENT_CLOSE if it was running), bound-waits for its
 * task to exit, and removes it from the ledger.
 */
error_t app_scheduler_stop(AppInstanceId app_instance_id, TickType_t join_timeout);

// app_scheduler_current_app_id() is public - see app/scheduler.h.

#ifdef __cplusplus
}
#endif
