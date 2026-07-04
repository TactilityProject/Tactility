#include <Tactility/service/ServiceRegistration.h>

#include <Tactility/Mutex.h>
#include <Tactility/service/ServiceInstance.h>
#include <Tactility/service/ServiceManifest.h>

#include <string>

#include <tactility/log.h>

namespace tt::service {

constexpr auto* TAG = "ServiceRegistration";

typedef std::unordered_map<std::string, std::shared_ptr<const ServiceManifest>> ManifestMap;
typedef std::unordered_map<std::string, std::shared_ptr<ServiceInstance>> ServiceInstanceMap;

static ManifestMap service_manifest_map;
static ServiceInstanceMap service_instance_map;

static Mutex manifest_mutex;
static Mutex instance_mutex;

void addService(std::shared_ptr<const ServiceManifest> manifest, bool autoStart) {
    assert(manifest != nullptr);
    // We'll move the manifest pointer, but we'll need to id later
    const auto& id = manifest->id;

    LOG_I(TAG, "Adding %s", id.c_str());

    manifest_mutex.lock();
    if (service_manifest_map[id] == nullptr) {
        service_manifest_map[id] = std::move(manifest);
    } else {
        LOG_E(TAG, "Service id in use: %s", id.c_str());
    }
    manifest_mutex.unlock();

    if (autoStart) {
        startService(id);
    }
}

void addService(const ServiceManifest& manifest, bool autoStart) {
    addService(std::make_shared<const ServiceManifest>(manifest), autoStart);
}

std::shared_ptr<const ServiceManifest> findManifestById(const std::string& id) {
    manifest_mutex.lock();
    auto iterator = service_manifest_map.find(id);
    auto manifest = iterator != service_manifest_map.end() ? iterator->second : nullptr;
    manifest_mutex.unlock();
    return manifest;
}

static std::shared_ptr<ServiceInstance> findServiceInstanceById(const std::string& id) {
    manifest_mutex.lock();
    auto iterator = service_instance_map.find(id);
    auto service = iterator != service_instance_map.end() ? iterator->second : nullptr;
    manifest_mutex.unlock();
    return service;
}

// TODO: Return proper error/status instead of BOOL?
bool startService(const std::string& id) {
    LOG_I(TAG, "Starting %s", id.c_str());
    auto manifest = findManifestById(id);
    if (manifest == nullptr) {
        LOG_E(TAG, "manifest not found for service %s", id.c_str());
        return false;
    }

    auto service_instance = std::make_shared<ServiceInstance>(manifest);

    // Register first, so that a service can retrieve itself during onStart()
    instance_mutex.lock();
    service_instance_map[manifest->id] = service_instance;
    instance_mutex.unlock();

    service_instance->setState(State::Starting);
    if (service_instance->getService()->onStart(*service_instance)) {
        service_instance->setState(State::Started);
    } else {
        LOG_E(TAG, "Starting %s failed", id.c_str());
        service_instance->setState(State::Stopped);
        instance_mutex.lock();
        service_instance_map.erase(manifest->id);
        instance_mutex.unlock();
    }

    LOG_I(TAG, "Started %s", id.c_str());

    return true;
}

std::shared_ptr<ServiceContext> findServiceContextById(const std::string& id) {
    return findServiceInstanceById(id);
}

std::shared_ptr<Service> findServiceById(const std::string& id) {
    auto instance = findServiceInstanceById(id);
    return instance != nullptr ? instance->getService() : nullptr;
}

bool stopService(const std::string& id) {
    LOG_I(TAG, "Stopping %s", id.c_str());
    auto service_instance = findServiceInstanceById(id);
    if (service_instance == nullptr) {
        LOG_W(TAG, "Service not running: %s", id.c_str());
        return false;
    }

    service_instance->setState(State::Stopping);
    service_instance->getService()->onStop(*service_instance);
    service_instance->setState(State::Stopped);

    instance_mutex.lock();
    service_instance_map.erase(id);
    instance_mutex.unlock();

    if (service_instance.use_count() > 1) {
        LOG_W(TAG, "Possible memory leak: service %s still has %d references", service_instance->getManifest().id.c_str(), (int)(service_instance.use_count() - 1));
    }

    LOG_I(TAG, "Stopped %s", id.c_str());

    return true;
}

State getState(const std::string& id) {
    auto service_instance = findServiceInstanceById(id);
    if (service_instance == nullptr) {
        return State::Stopped;
    }
    return service_instance->getState();
}

} // namespace
