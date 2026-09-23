// SPDX-License-Identifier: Apache-2.0
#pragma once

/**
 * This file contains functions to start and run apps.
 * It differs from start.h by running apps directly from the specified location,
 * instead of having to register them first via an AppManifest and the app manager.
 */

#include <app/manager.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Starts an app directly from @a location, without it having to be pre-registered via
 * app_manager_add() first.
 * Performs no checks of its own beyond what AppLoaderApi::load() itself rejects.
 * @warning It's advised to validate @a location with app_is_executable() first
 * @param[in] stack stack allocation config for the app's task; all-zero uses the scheduler's
 * default depth/capability, same as an AppManifest that leaves AppManifest::stack zeroed
 * @retval ERROR_NOT_FOUND no AppLoaderApi is registered for @a location.type
 * @retval ERROR_NONE on success
 */
[[deprecated("Use app_start_with_context()")]]
error_t app_execute(
    struct AppLocation location,
    struct AppStackConfig stack,
    int argc,
    const char* const argv[],
    AppInstanceId* out_app_instance_id
);

/**
 * Same as app_execute(), but as a modal child of @a parent_instance_id.
 * See app_start_for_result()'s own doc for the result-delivery contract.
 * @retval ERROR_NOT_FOUND no AppLoaderApi is registered for @a location.type
 * @retval ERROR_NONE on success
 */
[[deprecated("Use app_start_with_context()")]]
error_t app_execute_for_result(
    struct AppLocation location,
    struct AppStackConfig stack,
    int argc,
    const char* const argv[],
    AppInstanceId parent_instance_id,
    AppInstanceId* out_app_instance_id
);

/**
 * Same as app_execute(), but installs @a bindings into the new instance's fd table before its
 * task begins executing. See app_start_with_streams()'s own doc for stream ownership.
 * @param[in] bindings see app_start_with_streams()
 * @retval ERROR_NOT_FOUND no AppLoaderApi is registered for @a location.type
 * @retval ERROR_OUT_OF_RANGE a binding's producer_fd is out of range
 * @retval ERROR_RESOURCE a binding's event_group has no free bits left to claim
 * @retval ERROR_NONE on success
 */
[[deprecated("Use app_start_with_context()")]]
error_t app_execute_with_streams(
    struct AppLocation location,
    struct AppStackConfig stack,
    int argc,
    const char* const argv[],
    const struct AppStreamBinding* bindings,
    size_t binding_count,
    AppInstanceId* out_app_instance_id
);

/**
 * Combines app_execute_for_result() and app_execute_with_streams().
 * @param[in] bindings see app_start_with_streams()
 * @retval ERROR_NOT_FOUND no AppLoaderApi is registered for @a location.type
 * @retval ERROR_OUT_OF_RANGE a binding's producer_fd is out of range
 * @retval ERROR_RESOURCE a binding's event_group has no free bits left to claim
 * @retval ERROR_NONE on success
 */
[[deprecated("Use app_start_with_context()")]]
error_t app_execute_for_result_with_streams(
    struct AppLocation location,
    struct AppStackConfig stack,
    int argc,
    const char* const argv[],
    const struct AppStreamBinding* bindings,
    size_t binding_count,
    AppInstanceId parent_instance_id,
    AppInstanceId* out_app_instance_id
);

/**
 * Reports whether @a location is runnable on this target: the extension and header a loader
 * requires (e.g. an ELF's class/data/type/machine), not whether it lives anywhere in particular.
 * Any executable app is runnable from any path. Cheap enough to call while listing a directory.
 * @return false if @a location can't be run, or if no AppLoaderApi is registered for its type
 */
bool app_is_executable(struct AppLocation location);

bool app_is_executable_path(const char* path);

#ifdef __cplusplus
}
#endif
