// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <tactility/error.h>
#include <tactility/service/service_manifest.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ServiceInstanceInternal;

/** The lifecycle state of a ServiceInstance. */
typedef enum {
    SERVICE_STATE_STARTING,
    SERVICE_STATE_STARTED,
    SERVICE_STATE_STOPPING,
    SERVICE_STATE_STOPPED,
} ServiceState;

/** Represents a running (or about-to-run) instance of a service. */
struct ServiceInstance {
    /** The manifest that spawned this instance. */
    const struct ServiceManifest* manifest;
    /** The service created via manifest->create_service(). */
    struct Service* service;
    /**
     * Internal state managed by the kernel.
     * ServiceInstance implementers should initialize this to NULL.
     * Do not access or modify directly; use service_instance_* functions.
     */
    struct ServiceInstanceInternal* internal;
};

/**
 * @brief Construct a service instance.
 * @details This calls manifest->create_service() to build the service.
 * @param[in,out] instance a service instance with manifest set and internal set to NULL
 * @param[in] manifest non-null manifest to construct the instance from
 * @retval ERROR_OUT_OF_MEMORY if internal data allocation failed
 * @retval ERROR_NONE on success
 */
error_t service_instance_construct(struct ServiceInstance* instance, const struct ServiceManifest* manifest);

/**
 * @brief Destruct a service instance.
 * @details This calls manifest->destroy_service() on the service.
 * @param[in,out] instance non-null service instance pointer
 * @retval ERROR_INVALID_STATE if the instance is not stopped
 * @retval ERROR_NONE on success
 */
error_t service_instance_destruct(struct ServiceInstance* instance);

/**
 * @brief Get the manifest of a service instance.
 * @param[in] instance non-null service instance pointer
 * @return the manifest
 */
const struct ServiceManifest* service_instance_get_manifest(struct ServiceInstance* instance);

/**
 * @brief Get the service of a service instance.
 * @param[in] instance non-null service instance pointer
 * @return the service
 */
struct Service* service_instance_get_service(struct ServiceInstance* instance);

/**
 * @brief Get the state of a service instance.
 * @param[in] instance non-null service instance pointer
 * @return the current state
 */
ServiceState service_instance_get_state(struct ServiceInstance* instance);

#ifdef __cplusplus
}
#endif
