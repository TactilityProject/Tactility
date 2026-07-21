#pragma once

#include <memory>
#include <tactility/service/service_manifest.h>

struct ServiceInstance;

namespace tt::service {

struct ServiceManifest;
class ServicePaths;

/**
 * Thin, non-owning wrapper around the running TactilityKernel service instance
 * (ServiceInstance, which the kernel API also calls ServiceContext).
 * @warning Do not store references or pointers to these! You can retrieve them via the Loader service.
 */
class ServiceContext final {

    ::ServiceInstance* service_instance;

public:

    explicit ServiceContext(::ServiceInstance* instance) : service_instance(instance) {}

    /** @return a reference to the service's manifest */
    const ::ServiceManifest* getManifest() const;

    /** Retrieve the paths that are relevant to this service */
    std::unique_ptr<ServicePaths> getPaths() const;
};

} // namespace
