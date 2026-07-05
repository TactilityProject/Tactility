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
    /** Service-specific data, owned by the service implementation. Can be NULL. */
    void* data;
    /**
     * Called when the service is starting.
     * Can be NULL, in which case starting always succeeds.
     * @param[in,out] instance this service instance (also its own ServiceContext)
     * @return ERROR_NONE if the service started successfully
     */
    error_t (*on_start)(struct ServiceInstance* instance);
    /**
     * Called when the service is stopping.
     * Can be NULL, in which case stopping is a no-op.
     * @param[in,out] instance this service instance (also its own ServiceContext)
     */
    void (*on_stop)(struct ServiceInstance* instance);
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
 * @brief Get the data of a service instance.
 * @param[in] instance non-null service instance pointer
 * @return the data (can be NULL)
 */
void* service_instance_get_data(struct ServiceInstance* instance);

/**
 * @brief Get the state of a service instance.
 * @param[in] instance non-null service instance pointer
 * @return the current state
 */
ServiceState service_instance_get_state(struct ServiceInstance* instance);

/**
 * @brief Try to claim usage for this service instance. Increases reference count internally.
 * @param instance non-null service instance pointer
 * @return true when the instance is started and ref count was increased.
 */
bool service_instance_try_get(struct ServiceInstance* instance);

/**
 * @brief Release a claim for usage of this service instance. Decreases reference count internally.
 * @param instance non-null service instance pointer
 */
void service_instance_put(struct ServiceInstance* instance);

#ifdef __cplusplus
}
#endif
