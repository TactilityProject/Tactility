#include <Tactility/service/ServiceRegistration.h>

#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/ServiceManifest.h>

#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/service/service_manager.h>

#include <cassert>

namespace tt::service {

constexpr auto* TAG = "ServiceRegistration";

// Bridges the kernel's context-free C ServiceManifest/Service callbacks to the
// C++ Service instances they wrap. Declared extern "C" to match the linkage of
// the C function-pointer types they're assigned to (see e.g. gpio_controller.cpp).
extern "C" {

static error_t cppOnStartTrampoline(::ServiceInstance* cContext) {
    auto& servicePtr = *static_cast<std::shared_ptr<Service>*>(cContext->data);
    ServiceContext context(cContext);
    return servicePtr->onStart(context) ? ERROR_NONE : ERROR_RESOURCE;
}

static void cppOnStopTrampoline(::ServiceInstance* cContext) {
    auto& servicePtr = *static_cast<std::shared_ptr<Service>*>(cContext->data);
    ServiceContext context(cContext);
    servicePtr->onStop(context);
}

static void cppCreateServiceTrampoline(::ServiceInstance* cContext, void* context) {
    auto& cppManifest = *static_cast<std::shared_ptr<const ServiceManifest>*>(context);
    cContext->data = new std::shared_ptr(cppManifest->createService());
    cContext->on_start = cppOnStartTrampoline;
    cContext->on_stop = cppOnStopTrampoline;
}

static void cppDestroyServiceTrampoline(::ServiceInstance* cContext, void* /*context*/) {
    delete static_cast<std::shared_ptr<Service>*>(cContext->data);
}

} // extern "C"

void addService(std::shared_ptr<const ServiceManifest> manifest, bool autoStart) {
    assert(manifest != nullptr);
    const auto& id = manifest->id;

    LOG_I(TAG, "Adding %s", id.c_str());

    if (service_manager_find_manifest(id.c_str()) != nullptr) {
        LOG_E(TAG, "Service id in use: %s", id.c_str());
        return;
    }

    // The bridging C manifest and the C++ manifest it carries in .context are
    // intentionally never freed: services are registered once and live for the
    // process lifetime (there is no removeService()), matching the previous
    // implementation's static, never-erased manifest map.
    auto* cppManifestPtr = new std::shared_ptr(manifest);
    auto* cManifest = new ::ServiceManifest {
        .id = (*cppManifestPtr)->id.c_str(),
        .create_service = cppCreateServiceTrampoline,
        .destroy_service = cppDestroyServiceTrampoline,
        .context = cppManifestPtr
    };

    error_t error = service_manager_add(cManifest, autoStart);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to add service %s: %s", id.c_str(), error_to_string(error));
    }
}

void addService(const ServiceManifest& manifest, bool autoStart) {
    addService(std::make_shared<const ServiceManifest>(manifest), autoStart);
}

std::shared_ptr<const ServiceManifest> findManifestById(const std::string& id) {
    const auto* cManifest = service_manager_find_manifest(id.c_str());
    if (cManifest == nullptr) {
        return nullptr;
    }
    return *static_cast<std::shared_ptr<const ServiceManifest>*>(cManifest->context);
}

bool startService(const std::string& id) {
    LOG_I(TAG, "Starting %s", id.c_str());
    error_t error = service_manager_start(id.c_str());
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Starting %s failed: %s", id.c_str(), error_to_string(error));
        return false;
    }
    LOG_I(TAG, "Started %s", id.c_str());
    return true;
}

std::shared_ptr<ServiceContext> findServiceContextById(const std::string& id) {
    auto* cContext = service_manager_find_context(id.c_str());
    if (cContext == nullptr) {
        return nullptr;
    }
    return std::make_shared<ServiceContext>(cContext);
}

std::shared_ptr<Service> findServiceById(const std::string& id) {
    auto* cContext = service_manager_find_context(id.c_str());
    if (cContext == nullptr) {
        return nullptr;
    }
    return *static_cast<std::shared_ptr<Service>*>(cContext->data);
}

bool stopService(const std::string& id) {
    LOG_I(TAG, "Stopping %s", id.c_str());
    error_t error = service_manager_stop(id.c_str());
    if (error != ERROR_NONE) {
        LOG_W(TAG, "Service not running: %s", id.c_str());
        return false;
    }
    LOG_I(TAG, "Stopped %s", id.c_str());
    return true;
}

State getState(const std::string& id) {
    return service_manager_get_state(id.c_str());
}

} // namespace
