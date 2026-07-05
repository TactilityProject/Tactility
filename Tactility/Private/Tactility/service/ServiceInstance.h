#pragma once

#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServicePaths.h>

#include <tactility/service/service_context.h>

#include <memory>

namespace tt::service {

/**
 * Thin wrapper around the running TactilityKernel service context (::ServiceContext,
 * which is the same object as ::ServiceInstance in the C API). Non-owning: valid only
 * for as long as the underlying kernel instance is running.
 */
class ServiceInstance final : public ServiceContext {

    ::ServiceContext* cContext;

    std::shared_ptr<const ServiceManifest>& getCppManifest(::ServiceContext* cContext) const {
        const auto* cManifest = service_context_get_manifest(cContext);
        return *static_cast<std::shared_ptr<const ServiceManifest>*>(cManifest->context);
    }

public:

    explicit ServiceInstance(::ServiceContext* cContext) : cContext(cContext) {}
    ~ServiceInstance() override = default;

    /** @return a reference to the service's manifest */
    const ServiceManifest& getManifest() const override {
        return *getCppManifest(cContext);
    }

    /** Retrieve the paths that are relevant to this service */
    std::unique_ptr<ServicePaths> getPaths() const override {
        return std::make_unique<ServicePaths>(getCppManifest(cContext));
    }
};

}
