#pragma once

#include <memory>

struct ServiceInstance;

namespace tt::service {

struct ServiceManifest;
class ServicePaths;

/**
 * Thin, non-owning wrapper around the running TactilityKernel service instance
 * (::ServiceInstance, which the kernel API also calls ::ServiceContext).
 * @warning Do not store references or pointers to these! You can retrieve them via the Loader service.
 */
class ServiceContext final {

    ::ServiceInstance* cContext;

    std::shared_ptr<const ServiceManifest>& getCppManifest() const;

public:

    explicit ServiceContext(::ServiceInstance* cContext) : cContext(cContext) {}

    /** @return a reference to the service's manifest */
    const ServiceManifest& getManifest() const;

    /** Retrieve the paths that are relevant to this service */
    std::unique_ptr<ServicePaths> getPaths() const;
};

} // namespace
