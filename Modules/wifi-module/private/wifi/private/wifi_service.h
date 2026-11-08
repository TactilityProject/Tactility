// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>
#include <tactility/file_ops.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The service id, which also determines where the settings are stored */
#define WIFI_SERVICE_ID "tactility.wifi"

extern const struct ServiceManifest wifi_service_manifest;

/** The "wifi" command-line app */
extern const struct AppManifest wifi_command_manifest;

/** Imports the "*.ap.properties" files from the "provisioning" directory in the data path. */
void wifi_provisioning_import(void);

/**
 * Create a directory and its missing parents.
 * @retval ERROR_RESOURCE a directory could not be created
 * @retval ERROR_NONE on success
 */
error_t wifi_ensure_directory_exists(const char* path);

/**
 * Runs @a work on the file operations task and waits until it has finished, or runs it directly when
 * the caller is that task. Callers can have a stack in external RAM, which can't access flash.
 * @return false when the work could not be queued
 */
bool wifi_run_file_operation(file_ops_work_fn_t work, void* context);

#ifdef __cplusplus
}

#include <type_traits>

/** wifi_run_file_operation() for a callable, which can capture by reference as the work completes before this returns. */
template <typename Function>
bool wifi_run_on_file_task(Function&& function) {
    return wifi_run_file_operation(
        [](void* context) { (*static_cast<std::remove_reference_t<Function>*>(context))(); },
        &function
    );
}
#endif
