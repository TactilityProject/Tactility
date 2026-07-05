// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <tactility/service/service.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Allocates and initializes a new Service instance.
 * @param[in] context the manifest's context (ServiceManifest::context)
 * @return the new service, never NULL
 */
typedef struct Service* (*ServiceCreate)(void* context);

/**
 * Frees a Service instance that was created by the matching ServiceCreate function.
 * @param[in] service the service to free
 * @param[in] context the manifest's context (ServiceManifest::context)
 */
typedef void (*ServiceDestroy)(struct Service* service, void* context);

/**
 * Describes a registrable service type.
 * One manifest exists per service id, shared across start/stop cycles.
 */
struct ServiceManifest {
    /** Unique service identifier. Should never be NULL. */
    const char* id;
    /** Allocates and initializes a new Service instance. Should never be NULL. */
    ServiceCreate create_service;
    /** Frees a Service instance created by create_service. Should never be NULL. */
    ServiceDestroy destroy_service;
    /** Opaque context passed to create_service and destroy_service. Can be NULL. */
    void* context;
};

#ifdef __cplusplus
}
#endif
