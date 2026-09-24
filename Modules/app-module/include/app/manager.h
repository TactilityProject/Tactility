// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/instance.h>
#include <app/manifest.h>
#include <app/package_manifest.h>
#include <app/stream.h>

#include <tactility/error.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register an app manifest.
 * @retval ERROR_INVALID_ARGUMENT a manifest with the same id is already registered
 * @retval ERROR_NONE on success
 */
error_t app_manager_add(const struct AppManifest* manifest);

/**
 * Unregister a previously-added manifest.
 * @retval ERROR_NOT_FOUND no manifest with this id is registered
 * @retval ERROR_NONE on success
 */
error_t app_manager_remove(const char* id);

/**
 * @param[out] out_manifest set to a copy of the manifest on success
 * @retval ERROR_NOT_FOUND no manifest with this id is registered
 * @retval ERROR_NONE on success
 */
error_t app_manager_find_manifest(const char* id, struct AppManifest* out_manifest);

/**
 * Calls `@a` visitor once for every registered manifest. Iteration order is unspecified.
 * `@warning` `@a` visitor runs with app-module's internal registry lock held. Do not call any
 * app_manager_*() function from inside `@a` visitor - copy out what you need and act on it after
 * this call returns.
 */
typedef void (*AppManifestVisitorFn)(const struct AppManifest* manifest, void* context);
void app_manager_for_each_manifest(AppManifestVisitorFn visitor, void* context);

/**
 * Registers a package for enumeration via app_manager_for_each_package(). Separate from
 * registering its apps - the caller still calls app_manager_add() for each AppManifest.
 * @param[in] app_ids the ids of the AppManifest(s) this package registered
 * @retval ERROR_INVALID_ARGUMENT a package with the same id is already registered
 * @retval ERROR_NONE on success
 */
error_t app_manager_add_package(const struct PackageManifest* package, const char* const* app_ids, size_t app_id_count);

/**
 * Unregisters a previously-added package. Does not touch its apps' own registrations.
 * @retval ERROR_NOT_FOUND no package with this id is registered
 * @retval ERROR_NONE on success
 */
error_t app_manager_remove_package(const char* package_id);

/**
 * @param[out] out_package set to a copy of the package on success
 * @retval ERROR_NOT_FOUND no package with this id is registered
 * @retval ERROR_NONE on success
 */
error_t app_manager_find_package(const char* package_id, struct PackageManifest* out_package);

/** One registered package, handed to AppPackageVisitorFn - see app_manager_for_each_package(). */
struct AppPackage {
    struct PackageManifest package;
    /** How many entries @a app_ids points to. */
    size_t app_id_count;
    /** Valid only for the duration of the app_manager_for_each_package() call that produced
     * this - copy out what's needed before returning from the visitor. */
    const char* const* app_ids;
};

typedef void (*AppPackageVisitorFn)(const struct AppPackage* pkg, void* context);

/**
 * Calls @a visitor once for every registered package. Iteration order is unspecified.
 * @warning Same threading contract as app_manager_for_each_manifest(): runs with an internal
 * lock held - do not call any app_manager_*() function from inside @a visitor.
 */
void app_manager_for_each_package(AppPackageVisitorFn visitor, void* context);

/** One fd-to-stream binding for app_start_with_streams() (app/start.h). Every field is passed
 * through to app_stream_subscribe() as-is; see its own doc for the ownership contracts. */
struct AppStreamBinding {
    int producer_fd;
    struct AppStream* stream;
    void* buffer;
    size_t buffer_capacity;
    struct TaskEventGroup* event_group;
    /** Applied while subscribing, before the child task is created - a child's main() can run to
     * completion before app_start_with_context() even returns, so setting this on the stream
     * afterwards can already be too late. {0, 0} (the default) leaves it unset. */
    struct AppWindowSize window_size;
};

/**
 * Stop an app instance permanently. Emits APP_EVENT_CLOSE and bound-waits for its task to exit
 * if it was running.
 * @warning Must not be called from the instance's own task (it bound-waits via thread_join(),
 * which asserts against joining yourself) - an app closes itself by returning from its own
 * AppMainFn/AppLoaderApi::run(), not by calling this on itself.
 */
error_t app_manager_stop(AppInstanceId app_instance_id);

/** @return the instance's current state, or APP_INSTANCE_STATE_STOPPED if the id is unknown. */
AppInstanceState app_manager_get_state(AppInstanceId app_instance_id);

/**
 * @param[out] out_app_instance_id set to the instance id of the topmost currently-Active app -
 * the most recently started of whichever instances are Active (a modal child launched via
 * app_start_for_result() (app/start.h) stays Active alongside its parent while shown, so this
 * correctly picks the child, not the parent, while a dialog is up).
 * @retval ERROR_NOT_FOUND no app is Active
 * @retval ERROR_NONE on success
 */
error_t app_manager_get_topmost_instance_id(AppInstanceId* out_app_instance_id);

/**
 * Same as app_manager_get_topmost_instance_id(), but resolves straight to the topmost app's
 * manifest id string.
 * @param[out] buffer always NULL-terminated on return, even on failure (empty string if
 * @a buffer_size == 0 - nothing is written in that case; otherwise at least "" is written)
 * @retval ERROR_NOT_FOUND no app is Active
 * @retval ERROR_BUFFER_OVERFLOW @a buffer_size is too small to hold the id (including the NULL
 * terminator)
 * @retval ERROR_NONE on success
 */
error_t app_manager_get_topmost_app_id(char* buffer, size_t buffer_size);

/**
 * Registers @a path as a directory to scan for app manifests - each direct subdirectory of
 * @a path is expected to hold a manifest.properties (see app/package_manifest.h), matching the
 * layout app_install() creates ({install dir}/{package id}/manifest.properties), though this is not
 * install/uninstall - it only ever adds/removes manifest registrations, never touches files on
 * disk or running instances. No-op if @a path is already registered. Does not scan immediately -
 * call app_manager_install_path_scan() to do that.
 * @retval ERROR_NONE on success
 */
error_t app_manager_install_path_add(const char* path);

/**
 * Scans every path registered via app_manager_install_path_add(): registers
 * (app_manager_add()) any direct subdirectory with a valid manifest.properties that isn't
 * already registered, and unregisters (app_manager_remove() only - does not stop it if running,
 * does not delete anything) any manifest a previous scan registered whose directory has since
 * disappeared. Safe to call repeatedly (e.g. after an SD card is mounted/unmounted).
 */
void app_manager_install_path_scan(void);

/**
 * Uninstalls an app that was registered via app_manager_install_path_scan() (i.e. discovered on
 * disk, not installed via app_install()). Stops running instances, removes the manifest
 * registration, and deletes the app directory. Returns ERROR_NOT_FOUND if the app id is not in
 * the scan registry.
 */
error_t app_manager_install_path_uninstall(const char* app_id);

#ifdef __cplusplus
}
#endif
