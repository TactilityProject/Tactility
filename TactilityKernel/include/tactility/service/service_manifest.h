// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

// ServiceContext (declared in service_context.h) is a typedef alias of ServiceInstance,
// so the callback types below are declared directly in terms of ServiceInstance to avoid
// a conflicting forward-declaration of an unrelated "ServiceContext" struct tag.
struct ServiceInstance;

/**
 * Allocates and initializes the service's custom data.
 * @param[in] manifest the manifest that owns this callback
 * @return the service's custom data, or NULL if it has none
 */
typedef void* (*ServiceCreate)(const struct ServiceManifest* manifest);

/**
 * Frees custom data that was set up by the matching ServiceCreate function.
 * @param[in] data the custom data returned by create_service
 * @param[in] manifest the manifest that owns this callback
 */
typedef void (*ServiceDestroy)(const struct ServiceManifest* manifest, void* data);

/**
 * Called when a service instance is starting.
 * Can be NULL, in which case starting always succeeds.
 * @param[in,out] instance the starting service instance (also its own ServiceContext)
 * @param[in] data the custom data returned by create_service
 * @return ERROR_NONE if the service started successfully
 */
typedef error_t (*ServiceOnStart)(struct ServiceInstance* instance, void* data);

/**
 * Called when a service instance is stopping.
 * Can be NULL, in which case stopping is a no-op.
 * @param[in,out] instance the stopping service instance (also its own ServiceContext)
 * @param[in] data the custom data returned by create_service
 */
typedef void (*ServiceOnStop)(struct ServiceInstance* instance, void* data);

/**
 * Describes a registrable service type.
 * One manifest exists per service id, shared across start/stop cycles.
 */
struct ServiceManifest {
    /** Unique service identifier. Should never be NULL. */
    const char* id;
    /** Allocates the service's custom data. Should never be NULL. */
    ServiceCreate create_service;
    /** Frees the service's custom data. Should never be NULL. */
    ServiceDestroy destroy_service;
    /** Called when a service instance starts. Can be NULL. */
    ServiceOnStart on_start;
    /** Called when a service instance stops. Can be NULL. */
    ServiceOnStop on_stop;
};

#ifdef __cplusplus
}
#endif
