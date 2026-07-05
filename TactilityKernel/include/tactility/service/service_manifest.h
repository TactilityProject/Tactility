// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

// ServiceContext (declared in service_context.h) is a typedef alias of ServiceInstance,
// so ServiceCreate/ServiceDestroy below are declared directly in terms of ServiceInstance
// to avoid a conflicting forward-declaration of an unrelated "ServiceContext" struct tag.
struct ServiceInstance;

/**
 * Initializes a newly-registered service instance in place.
 * Implementations should set instance->data, instance->on_start and instance->on_stop
 * as needed; all three may be left at their zeroed defaults (NULL) if the service has
 * no state or lifecycle callbacks.
 * @param[in,out] instance the instance to populate (manifest and internal are already set)
 * @param[in] context the manifest's context (ServiceManifest::context)
 */
typedef void (*ServiceCreate)(struct ServiceInstance* instance, void* context);

/**
 * Tears down state that was set up by the matching ServiceCreate function
 * (e.g. frees instance->data). Should not clear instance->data/on_start/on_stop;
 * the caller does that.
 * @param[in,out] instance the instance to tear down
 * @param[in] context the manifest's context (ServiceManifest::context)
 */
typedef void (*ServiceDestroy)(struct ServiceInstance* instance, void* context);

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
