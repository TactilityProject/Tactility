// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/instance.h>
#include <app/manifest.h>

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

/** @return the manifest, or NULL if not found. */
const struct AppManifest* app_manager_find_manifest(const char* id);

/**
 * Calls `@a` visitor once for every registered manifest. Iteration order is unspecified.
 * `@warning` `@a` visitor runs with app-module's internal registry lock held. Do not call any
 * app_manager_*() function from inside `@a` visitor - copy out what you need and act on it after
 * this call returns.
 */
typedef void (*AppManifestVisitorFn)(const struct AppManifest* manifest, void* context);
void app_manager_for_each_manifest(AppManifestVisitorFn visitor, void* context);

/**
 * Starts a new instance of the app registered under @a id. Every app instance gets its own
 * dedicated task for its entire lifetime - starting an app never asks any other app to give up
 * its task, and multiple instances (of the same or different apps) can be Active at once.
 * @param[in] id the manifest id to start
 * @param[out] out_app_instance_id the id of the new app instance
 * @retval ERROR_NOT_FOUND no manifest with this id is registered, or no AppLoaderApi is registered
 * @retval ERROR_NONE on success
 */
error_t app_manager_start(const char* id, AppInstanceId* out_app_instance_id);

/**
 * Same as app_manager_start(), but also passes @a argc/@a argv to the new instance's own main
 * function (see app/loader.h's AppMainFn) - modelled on a C program's main(argc, argv). For
 * regular (non-modal) navigations that need to pass data to the target app (e.g. "show details
 * for this app id") without expecting a result back.
 * @param[in] argv @a argc strings; app-module makes its own deep copy before returning, so
 * @a argv and the strings it points to may be freed/go out of scope immediately after this call
 * returns (e.g. safe to pass a stack-local array of a caller's own std::string::c_str()s).
 */
error_t app_manager_start_with_parameters(const char* id, int argc, const char* const argv[], AppInstanceId* out_app_instance_id);

/**
 * Starts @a id as a modal child of @a parent_instance_id, for the purpose of receiving a
 * result. The parent keeps running (window_manager's own multi-window stack handles burying its
 * window while the child is shown).
 *
 * When the child's task exits, an APP_EVENT_RESULT is delivered to @a parent_instance_id -
 * result is whatever the child's AppMainFn/AppLoaderApi::run() returned - unless
 * @a parent_instance_id is 0, in which case no result is delivered (fire-and-forget, for
 * callers with no app_instance_id of their own). The parent is then responsible for calling
 * app_manager_stop() on the child's instance id to fully reap it. Children that need to hand
 * back more than an int32_t (e.g. picked text, a path) expose their own "get last result"
 * getter for the parent to call after receiving the event - see e.g.
 * tt::app::inputdialog::getLastText().
 * @param[in] argv @a argc strings; app-module makes its own deep copy before returning (same as
 * app_manager_start_with_parameters()), so @a argv and the strings it points to may be
 * freed/go out of scope immediately after this call returns.
 * @retval ERROR_NOT_FOUND no manifest with this id is registered, or no AppLoaderApi is registered
 * @retval ERROR_NONE on success
 */
error_t app_manager_start_for_result(const char* id, AppInstanceId parent_instance_id, int argc, const char* const argv[], AppInstanceId* out_app_instance_id);

/**
 * Stop an app instance permanently. Emits APP_EVENT_CLOSE and bound-waits for its task to exit
 * if it was running.
 * @warning Must not be called from the instance's own task (it bound-waits via thread_join(),
 * which asserts against joining yourself) - an app closing itself must call app_manager_finish()
 * instead, right before returning from its own AppMainFn/AppLoaderApi::run().
 */
error_t app_manager_stop(AppInstanceId app_instance_id);

/**
 * Called by an app instance, from its own task, right before it returns in response to
 * APP_EVENT_CLOSE - whether that close was self-initiated (e.g. its own back button) or came
 * from someone else. Marks this instance Stopped immediately (rather than waiting for its task
 * to actually exit) so app_manager_get_state()/app_manager_get_topmost_instance_id() reflect the
 * closure as soon as the app has decided to close, not just once its task has fully unwound.
 * @warning Does not join or free this instance's own task/ledger entry (can't - this runs on
 * that very task); those are cleaned up on a later app_manager_stop() call, same as any
 * self-terminating instance.
 */
error_t app_manager_finish(AppInstanceId app_instance_id);

/** @return the instance's current state, or APP_INSTANCE_STATE_STOPPED if the id is unknown. */
AppInstanceState app_manager_get_state(AppInstanceId app_instance_id);

/**
 * @param[out] out_app_instance_id set to the instance id of the topmost currently-Active app -
 * the most recently started of whichever instances are Active (a modal child launched via
 * app_manager_start_for_result() stays Active alongside its parent while shown, so this
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
 * @a path is expected to hold a manifest.properties (see app/metadata.h), matching the layout
 * app_install() creates ({install dir}/{app_id}/manifest.properties), though this is not
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

#ifdef __cplusplus
}
#endif
