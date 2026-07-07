#include <Tactility/service/ServiceContext.h>

#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServicePaths.h>

#include <tactility/service/service_context.h>

namespace tt::service {

std::shared_ptr<const ServiceManifest>& ServiceContext::getCppManifest() const {
    const auto* cManifest = service_context_get_manifest(cContext);
    return *static_cast<std::shared_ptr<const ServiceManifest>*>(cManifest->context);
}

const ServiceManifest& ServiceContext::getManifest() const {
    return *getCppManifest();
}

std::unique_ptr<ServicePaths> ServiceContext::getPaths() const {
    return std::make_unique<ServicePaths>(getCppManifest());
}

} // namespace
