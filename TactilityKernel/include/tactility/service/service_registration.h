// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <tactility/error.h>
#include <tactility/service/service_context.h>
#include <tactility/service/service_instance.h>
#include <tactility/service/service_manifest.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register a service manifest.
 * @param[in] manifest non-null manifest to register
 * @param[in] auto_start if true, the service is started immediately after registration
 * @retval ERROR_INVALID_ARGUMENT if a manifest with the same id is already registered
 * @retval ERROR_RESOURCE if auto_start is true and starting the service failed
 * @retval ERROR_NONE on success
 */
error_t service_registration_add(const struct ServiceManifest* manifest, bool auto_start);

/**
 * @brief Unregister a previously-added manifest.
 * @param[in] id non-null service id
 * @retval ERROR_INVALID_STATE if the service is still running
 * @retval ERROR_NOT_FOUND if no manifest with this id is registered
 * @retval ERROR_NONE on success
 */
error_t service_registration_remove(const char* id);

/**
 * @brief Start a registered service by id.
 * @param[in] id non-null service id
 * @retval ERROR_NOT_FOUND if no manifest with this id is registered
 * @retval ERROR_INVALID_STATE if the service is already running
 * @retval ERROR_RESOURCE if the service's on_start callback failed
 * @retval ERROR_NONE on success
 */
error_t service_registration_start(const char* id);

/**
 * @brief Stop a running service by id.
 * @param[in] id non-null service id
 * @retval ERROR_NOT_FOUND if no service with this id is running
 * @retval ERROR_NONE on success
 */
error_t service_registration_stop(const char* id);

/**
 * @brief Get the state of a service by id.
 * @param[in] id non-null service id
 * @return the current state, or SERVICE_STATE_STOPPED if the id is unknown
 */
ServiceState service_registration_get_state(const char* id);

/**
 * @brief Find a registered manifest by id.
 * @param[in] id non-null service id
 * @return the manifest, or NULL if not found
 */
const struct ServiceManifest* service_registration_find_manifest(const char* id);

/**
 * @brief Find the context of a running service by id.
 * @param[in] id non-null service id
 * @return the context, or NULL if the service isn't running
 */
ServiceContext* service_registration_find_context(const char* id);

/**
 * @brief Find the service instance of a running service by id.
 * @param[in] id non-null service id
 * @return the service, or NULL if not running
 */
struct Service* service_registration_find_service(const char* id);

#ifdef __cplusplus
}
#endif
