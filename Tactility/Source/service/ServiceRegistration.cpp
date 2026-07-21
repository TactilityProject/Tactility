#include <Tactility/service/ServiceRegistration.h>

#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/ServiceManifest.h>

#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/service/service_manager.h>

#include <cassert>
#include <memory>

namespace tt::service {

constexpr auto* TAG = "ServiceRegistration";

// Bridges the kernel's C ServiceManifest/Service callbacks to the C++ Service
// instances they wrap. Declared extern "C" to match the linkage of the C
// function-pointer types they're assigned to (see e.g. gpio_controller.cpp).
extern "C" {

static void cppDestroyServiceTrampoline(const ::ServiceManifest* /*manifest*/, void* data) {
    delete static_cast<std::shared_ptr<Service>*>(data);
}

static error_t cppOnStartTrampoline(::ServiceInstance* instance, void* data) {
    auto& servicePtr = *static_cast<std::shared_ptr<Service>*>(data);
    ServiceContext context(instance);
    return servicePtr->onStart(context) ? ERROR_NONE : ERROR_RESOURCE;
}

static void cppOnStopTrampoline(::ServiceInstance* instance, void* data) {
    auto& servicePtr = *static_cast<std::shared_ptr<Service>*>(data);
    ServiceContext context(instance);
    servicePtr->onStop(context);
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

    // Intentionally never freed: services are registered once and live for the
    // process lifetime (there is no removeService()). Keeps id's backing string
    // alive for cManifest.id below.
    auto* persistentManifest = new std::shared_ptr(manifest);
    auto* cManifest = new ::ServiceManifest {
        .id = (*persistentManifest)->id.c_str(),
        .create_service = (*persistentManifest)->createService,
        .destroy_service = cppDestroyServiceTrampoline,
        .on_start = cppOnStartTrampoline,
        .on_stop = cppOnStopTrampoline,
    };

    error_t error = service_manager_add(cManifest, autoStart);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to add service %s: %s", id.c_str(), error_to_string(error));
    }
}

void addService(const ServiceManifest& manifest, bool autoStart) {
    addService(std::make_shared<const ServiceManifest>(manifest), autoStart);
}

const ::ServiceManifest* findManifestById(const std::string& id) {
    return service_manager_find_manifest(id.c_str());
}

bool startService(const std::string& id) {
    LOG_I(TAG, "Starting %s", id.c_str());
    const error_t error = service_manager_start(id.c_str());
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Starting %s failed: %s", id.c_str(), error_to_string(error));
        return false;
    }
    LOG_I(TAG, "Started %s", id.c_str());
    return true;
}

std::shared_ptr<ServiceContext> findServiceContextById(const std::string& id) {
    auto* instance = service_manager_find_instance(id.c_str());
    if (instance == nullptr) {
        return nullptr;
    }
    return std::make_shared<ServiceContext>(instance);
}

std::shared_ptr<Service> findServiceById(const std::string& id) {
    auto* instance = service_manager_find_instance(id.c_str());
    if (instance == nullptr) {
        return nullptr;
    }
    return *static_cast<std::shared_ptr<Service>*>(instance->data);
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
