// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/manifest.h>
#include <tactility/error.h>
#include <stdint.h>
#include "location.h"

#ifdef __cplusplus
extern "C" {
#endif

/** service-module id the AppLoaderApi implementation for AppManifest::location.type ==
 * APP_LOCATION_MEMORY must register under. Implemented by app-module itself (source/app_internal_loader.cpp). */
#define APP_LOADER_MEMORY_SERVICE_ID "app-loader-memory"

/** service-module id the AppLoaderApi implementation for AppManifest::location.type ==
 * APP_LOCATION_PATH must register under. Implemented by a platform module (e.g. app-esp32-module). */
#define APP_LOADER_PATH_SERVICE_ID "app-loader-path"

/**
 * Entry point signature for an APP_LOCATION_MEMORY app: a function linked directly into this
 * firmware binary. Called on the dedicated task app-module's scheduler spawns for this instance,
 * blocking for the app's whole lifetime - same contract as an external app's main(), plus
 * @a app_instance_id identifying this running instance (use it with
 * app_event_subscribe()/window_manager_create()/app_manager_finish()/etc.).
 * AppManifest::location.location holds this cast to void*.
 */
typedef int32_t (*AppMainFn)(uint32_t app_instance_id, int argc, char* argv[]);

typedef void* AppRuntime;

/**
 * Pluggable mechanism for loading and executing an app.
 */
struct AppLoaderApi {
    /**
     * Prepares an app instance for execution (e.g. read + relocate its binary).
     * @param[in] location the location to load the elf from
     * @param[out] out_runtime opaque handle to whatever load() allocated; passed back to run()/unload()
     */
    error_t (*load)(struct AppLocation location, AppRuntime* out_runtime);

    /**
     * Blocking: runs the app to completion.
     * @param[in] runtime handle produced by load()
     * @param[in] app_instance_id the running instance's id
     * @param[in] argc the amount of arguments in @a argv
     * @param[in] argv the array of string pointers (can be NULL)
     */
    int32_t (*run)(AppRuntime runtime, uint32_t app_instance_id, int argc, char* argv[]);

    /** Releases whatever load() allocated. Called after run() returns. */
    void (*unload)(AppRuntime runtime);
};

#ifdef __cplusplus
}
#endif
