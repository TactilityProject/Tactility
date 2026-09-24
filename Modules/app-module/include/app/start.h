// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/manager.h>

/**
 * This file contains functions to start and run apps.
 * When an AppManifest is provided, it can start apps that were registered with the app manager.
 * It can also start app binaries directly.
 *
 * Steps:
 *  - Create an AppStartContext by using one of the helper functions:
 *    - app_start_context_for_manifest() for manager-registered apps
 *    - app_start_context_for_location() for plain binaries
 *  - Optionally modify AppStartContext with with one of the helper functions. (e.g. to add parameters)
 *  - Call app_start_with_context() to start the execution.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Identifies what app_manager_start_internal() should launch and how.
 * Construct via app_start_context_from_manifest()/app_start_context_from_location()
 * @warning Fields are for internal use only. Do not read or write them directly.
 */
struct AppStartContext {
    /** Empty string means no manifest, which means it's a location-based binary start.
     * If it's non-empty, then the app is represented by a package and it will run in a context where
     * features like app_paths_* are available. */
    AppId id;
    struct AppLocation location;
    struct AppStackConfig stack;
    int argc;
    const char* const* argv;
    const struct AppStreamBinding* bindings;
    size_t binding_count;
    AppInstanceId parent_id;
    /** NULL-terminated array of "KEY=VALUE" strings, like POSIX environ. */
    char* const* environment;
};

/** Builds a context for starting a manifest already registered via app_manager_add(). */
struct AppStartContext app_start_context_for_manifest(const struct AppManifest* manifest);

/** Builds a context for starting @a location directly, with no registered manifest.
 * Stack defaults to zeroed (the scheduler's default). */
struct AppStartContext app_start_context_for_location(struct AppLocation location);

/**
 * Looks up @a id in the manifest registry and builds a context for it (see
 * app_start_context_from_manifest()).
 * @retval ERROR_NOT_FOUND no manifest with this id is registered
 * @retval ERROR_NONE on success
 */
error_t app_start_context_from_id(const char* id, struct AppStartContext* out_context);

/** Overrides the task's stack allocation config (defaults to the manifest's own, or zeroed/
 * scheduler-default for a location-based context). See AppStackConfig. */
void app_start_context_set_stack(struct AppStartContext* context, struct AppStackConfig stack);

/** @param[in] argv @a argc strings, borrowed only until app_manager_start_internal() returns. It makes its own deep copy. */
void app_start_context_set_arguments_ext(struct AppStartContext* context, int argc, const char* const argv[]);

/** @param[in] arguments null-terminated string array, borrowed only until app_manager_start_internal() returns. It makes its own deep copy. */
void app_start_context_set_arguments(struct AppStartContext* context, const char* const arguments[]);

/** @param[in] bindings @a binding_count entries; see app_start_with_streams() for ownership. */
void app_start_context_set_streams(struct AppStartContext* context, const struct AppStreamBinding* bindings, size_t binding_count);

/** See app_start_for_result() for the parent/result-delivery contract. */
void app_start_context_set_parent(struct AppStartContext* context, AppInstanceId parent_id);

/** @param[in] environment NULL-terminated array of "KEY=VALUE" strings, borrowed only until
 * app_start_with_context() returns. */
void app_start_context_set_environment(struct AppStartContext* context, char* const environment[]);

/**
 * Starts an app from @a context, built via app_start_context_for_manifest()/
 * app_start_context_for_location()/app_start_context_from_id() and app_start_context_set_*().
 * Replaces app_start()/app_start_for_result()/app_start_with_streams()/
 * app_start_for_result_with_streams().
 * @retval ERROR_NOT_FOUND no AppLoaderApi is registered for @a context->location.type
 * @retval ERROR_OUT_OF_RANGE a binding's producer_fd is out of range
 * @retval ERROR_RESOURCE a binding's event_group has no free bits left to claim
 * @retval ERROR_NONE on success
 */
error_t app_start_with_context(struct AppStartContext* context, AppInstanceId* out_app_instance_id);

/**
 * Starts @a id, a manifest already registered via app_manager_add(), passing @a argc/@a argv to
 * the new instance's own main function (see app/loader.h's AppMainFn), modelled on a C program's
 * main(argc, argv). For regular (non-modal) navigations that need to pass data to the target app
 * (e.g. "show details for this app id") without expecting a result back.
 * @param[in] argv @a argc strings; app-module makes its own deep copy before returning, so
 * @a argv and the strings it points to may be freed/go out of scope immediately after this call
 * returns (e.g. safe to pass a stack-local array of a caller's own std::string::c_str()s).
 * @retval ERROR_NOT_FOUND no manifest with this id is registered, or no AppLoaderApi is registered
 * @retval ERROR_NONE on success
 */
[[deprecated("Use app_start_with_context()")]]
error_t app_start(const char* id, int argc, const char* const argv[], AppInstanceId* out_app_instance_id);

/**
 * Starts @a id as a child of @a parent_instance_id, for the purpose of receiving a result.
 *
 * When the child's task exits, an APP_EVENT_RESULT is delivered to @a parent_instance_id.
 * The result is whatever the child's AppMainFn/AppLoaderApi::run() returned, unless
 * @a parent_instance_id is 0, in which case no result is delivered (fire-and-forget, for
 * callers with no app_instance_id of their own). The parent is then responsible for calling
 * app_manager_stop() on the child's instance id to fully reap it.
 *
 * @param[in] argv @a argc strings; app-module makes its own deep copy before returning (same as
 * app_start()), so @a argv and the strings it points to may be
 * freed/go out of scope immediately after this call returns.
 * @retval ERROR_NOT_FOUND no manifest with this id is registered, or no AppLoaderApi is registered
 * @retval ERROR_NONE on success
 */
[[deprecated("Use app_start_with_context()")]]
error_t app_start_for_result(const char* id, int argc, const char* const argv[], AppInstanceId parent_instance_id, AppInstanceId* out_app_instance_id);

/**
 * Same as app_start(), but installs @a bindings into the new instance's fd table before
 * its task begins executing (e.g. a child's stdio, piped through parent-owned AppStreams; see
 * app/stream.h). Writes the new instance's id into each bound stream's producer_id itself, since
 * the caller cannot know it in advance.
 * @param[in] bindings @a binding_count entries; each stream and buffer must stay alive (see
 * app_stream_subscribe()) until unsubscribed or the child exits.
 * @retval ERROR_NOT_FOUND no manifest with this id is registered, or no AppLoaderApi is registered
 * @retval ERROR_OUT_OF_RANGE a binding's producer_fd is out of range
 * @retval ERROR_RESOURCE a binding's event_group has no free bits left to claim
 * @retval ERROR_NONE on success
 */
[[deprecated("Use app_start_with_context()")]]
error_t app_start_with_streams(const char* id, const struct AppStreamBinding* bindings, size_t binding_count, AppInstanceId* out_app_instance_id);

/**
 * Combines app_start_for_result() and app_start_with_streams(): starts @a id as
 * a modal child of @a parent_instance_id (see app_start_for_result()'s own doc for the
 * result-delivery contract) with @a bindings installed into its fd table before its task begins
 * executing (see app_start_with_streams()'s own doc for stream ownership).
 * For a child that needs to hand back more than an int32_t (e.g. a path) via its own stdout.
 * @param[in] argv see app_start_for_result().
 * @param[in] bindings see app_start_with_streams().
 * @retval ERROR_NOT_FOUND no manifest with this id is registered, or no AppLoaderApi is registered
 * @retval ERROR_OUT_OF_RANGE a binding's producer_fd is out of range
 * @retval ERROR_RESOURCE a binding's event_group has no free bits left to claim
 * @retval ERROR_NONE on success
 */
[[deprecated("Use app_start_with_context()")]]
error_t app_start_for_result_with_streams(const char* id, int argc, const char* const argv[], const struct AppStreamBinding* bindings, size_t binding_count, AppInstanceId parent_instance_id, AppInstanceId* out_app_instance_id);

#ifdef __cplusplus
}
#endif
