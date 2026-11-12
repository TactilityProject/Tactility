#include <service/manager.h>

#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/Mutex.h>
#include <tactility/Memory.hpp>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/ServiceManifest.h>

#include <tactility/error.h>
#include <tactility/log.h>

#include <cassert>
#include <memory>
#include <unordered_map>

namespace tt::service {

constexpr auto* TAG = "ServiceRegistration";

namespace {

// Tracks the heap allocations addService() makes per registered id, so removeService()
// can free them once the kernel confirms the manifest is unregistered. The kernel only
// ever stores the raw pointer it's handed (see service_manager_add/_remove) - it never
// takes ownership - so the registering side (us) is responsible for the lifetime.
struct AllocatedManifest {
    std::shared_ptr<const ServiceManifest>* persistentManifest;
    ::ServiceManifest* cManifest;
};

Mutex& allocatedManifestsMutex() {
    static Mutex mutex;
    return mutex;
}

std::unordered_map<std::string, AllocatedManifest>& allocatedManifests() {
    static std::unordered_map<std::string, AllocatedManifest> map;
    return map;
}

} // namespace

// Bridges the kernel's C ServiceManifest/Service callbacks to the C++ Service
// instances they wrap. Declared extern "C" to match the linkage of the C
// function-pointer types they're assigned to (see e.g. gpio_controller.cpp).
extern "C" {

// Releases what ServiceManifest::createService returned, which must be allocated with tt::newObject()
static void cppDestroyServiceTrampoline(const ::ServiceManifest* /*manifest*/, void* data) {
    deleteObject(static_cast<std::shared_ptr<Service>*>(data));
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

    // Freed by removeService() once the kernel confirms the manifest is unregistered.
    // Keeps id's backing string alive for cManifest.id below in the meantime.
    auto* persistentManifest = new std::shared_ptr(manifest);
    auto* cManifest = new ::ServiceManifest {
        .id = (*persistentManifest)->id.c_str(),
        .create_service = (*persistentManifest)->createService,
        .destroy_service = cppDestroyServiceTrampoline,
        .on_start = cppOnStartTrampoline,
        .on_stop = cppOnStopTrampoline,
    };

    {
        auto lock = allocatedManifestsMutex().asScopedLock();
        lock.lock();
        allocatedManifests()[id] = AllocatedManifest { persistentManifest, cManifest };
    }

    error_t error = service_manager_add(cManifest, autoStart);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to add service %s: %s", id.c_str(), error_to_string(error));
    }
}

void addService(const ServiceManifest& manifest, bool autoStart) {
    addService(std::make_shared<const ServiceManifest>(manifest), autoStart);
}

bool removeService(const std::string& id) {
    if (service_manager_get_state(id.c_str()) != SERVICE_STATE_STOPPED) {
        LOG_I(TAG, "Stopping %s before removal", id.c_str());
        if (!stopService(id)) {
            LOG_E(TAG, "Failed to stop %s before removal", id.c_str());
            return false;
        }
    }

    LOG_I(TAG, "Removing %s", id.c_str());
    error_t error = service_manager_remove(id.c_str());
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to remove service %s: %s", id.c_str(), error_to_string(error));
        return false;
    }

    // The kernel has confirmed the manifest is unregistered, so id (which points into
    // persistentManifest's string) is no longer needed by anything - safe to free now.
    {
        auto lock = allocatedManifestsMutex().asScopedLock();
        lock.lock();
        auto iterator = allocatedManifests().find(id);
        if (iterator != allocatedManifests().end()) {
            delete iterator->second.cManifest;
            delete iterator->second.persistentManifest;
            allocatedManifests().erase(iterator);
        }
    }

    LOG_I(TAG, "Removed %s", id.c_str());
    return true;
}

bool removeService(const ServiceManifest& manifest) {
    return removeService(manifest.id);
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
