// SPDX-License-Identifier: Apache-2.0

#include <tactility/service/service_manager.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/log.h>

#include <new>
#include <string>
#include <unordered_map>

#define TAG "service_registration"

// Defined in service_instance.cpp. Internal-only: lets the registry drive state
// transitions without exposing mutation on the public ServiceInstance API.
extern "C" void service_instance_set_state(ServiceInstance* instance, ServiceState state);

struct ManifestLedger {
    std::unordered_map<std::string, const ServiceManifest*> manifests;
    Mutex mutex {};

    ManifestLedger() { mutex_construct(&mutex); }
    ~ManifestLedger() { mutex_destruct(&mutex); }
};

struct InstanceLedger {
    std::unordered_map<std::string, ServiceInstance*> instances;
    Mutex mutex {};

    InstanceLedger() { mutex_construct(&mutex); }
    ~InstanceLedger() { mutex_destruct(&mutex); }
};

static ManifestLedger& get_manifest_ledger() {
    static ManifestLedger ledger;
    return ledger;
}

static InstanceLedger& get_instance_ledger() {
    static InstanceLedger ledger;
    return ledger;
}

#define manifest_ledger get_manifest_ledger()
#define instance_ledger get_instance_ledger()

extern "C" {

error_t service_manager_add(const ServiceManifest* manifest, bool auto_start) {
    mutex_lock(&manifest_ledger.mutex);
    if (manifest_ledger.manifests.contains(manifest->id)) {
        mutex_unlock(&manifest_ledger.mutex);
        LOG_E(TAG, "Manifest with id '%s' is already registered", manifest->id);
        return ERROR_INVALID_ARGUMENT;
    }
    manifest_ledger.manifests[manifest->id] = manifest;
    mutex_unlock(&manifest_ledger.mutex);

    LOG_I(TAG, "add %s", manifest->id);

    if (auto_start) {
        return service_manager_start(manifest->id);
    }

    return ERROR_NONE;
}

error_t service_manager_remove(const char* id) {
    if (service_manager_find_context(id) != nullptr) {
        return ERROR_INVALID_STATE;
    }

    mutex_lock(&manifest_ledger.mutex);
    const auto iterator = manifest_ledger.manifests.find(id);
    if (iterator == manifest_ledger.manifests.end()) {
        mutex_unlock(&manifest_ledger.mutex);
        return ERROR_NOT_FOUND;
    }
    manifest_ledger.manifests.erase(iterator);
    mutex_unlock(&manifest_ledger.mutex);

    LOG_I(TAG, "remove %s", id);
    return ERROR_NONE;
}

error_t service_manager_start(const char* id) {
    mutex_lock(&manifest_ledger.mutex);
    const auto manifest_iterator = manifest_ledger.manifests.find(id);
    if (manifest_iterator == manifest_ledger.manifests.end()) {
        mutex_unlock(&manifest_ledger.mutex);
        return ERROR_NOT_FOUND;
    }
    const ServiceManifest* manifest = manifest_iterator->second;
    mutex_unlock(&manifest_ledger.mutex);

    mutex_lock(&instance_ledger.mutex);
    if (instance_ledger.instances.contains(id)) {
        mutex_unlock(&instance_ledger.mutex);
        return ERROR_INVALID_STATE;
    }

    auto* instance = new(std::nothrow) ServiceInstance { .manifest = nullptr, .data = nullptr, .on_start = nullptr, .on_stop = nullptr, .internal = nullptr };
    if (instance == nullptr) {
        mutex_unlock(&instance_ledger.mutex);
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = service_instance_construct(instance, manifest);
    if (error != ERROR_NONE) {
        mutex_unlock(&instance_ledger.mutex);
        delete instance;
        return error;
    }

    // Register before on_start() so a service can find itself while starting.
    instance_ledger.instances[id] = instance;
    mutex_unlock(&instance_ledger.mutex);

    service_instance_set_state(instance, SERVICE_STATE_STARTING);

    LOG_I(TAG, "start %s", id);
    error = (instance->on_start != nullptr) ? instance->on_start(instance) : ERROR_NONE;

    if (error == ERROR_NONE) {
        service_instance_set_state(instance, SERVICE_STATE_STARTED);
        return ERROR_NONE;
    }

    LOG_E(TAG, "Failed to start %s: %s", id, error_to_string(error));
    service_instance_set_state(instance, SERVICE_STATE_STOPPED);

    mutex_lock(&instance_ledger.mutex);
    instance_ledger.instances.erase(id);
    mutex_unlock(&instance_ledger.mutex);

    service_instance_destruct(instance);
    delete instance;

    return ERROR_RESOURCE;
}

error_t service_manager_stop(const char* id) {
    mutex_lock(&instance_ledger.mutex);
    const auto iterator = instance_ledger.instances.find(id);
    if (iterator == instance_ledger.instances.end()) {
        mutex_unlock(&instance_ledger.mutex);
        return ERROR_NOT_FOUND;
    }
    ServiceInstance* instance = iterator->second;
    mutex_unlock(&instance_ledger.mutex);

    LOG_I(TAG, "stop %s", id);

    service_instance_set_state(instance, SERVICE_STATE_STOPPING);

    if (instance->on_stop != nullptr) {
        instance->on_stop(instance);
    }

    service_instance_set_state(instance, SERVICE_STATE_STOPPED);

    mutex_lock(&instance_ledger.mutex);
    instance_ledger.instances.erase(id);
    mutex_unlock(&instance_ledger.mutex);

    service_instance_destruct(instance);
    delete instance;

    return ERROR_NONE;
}

ServiceState service_manager_get_state(const char* id) {
    mutex_lock(&instance_ledger.mutex);
    const auto iterator = instance_ledger.instances.find(id);
    if (iterator == instance_ledger.instances.end()) {
        mutex_unlock(&instance_ledger.mutex);
        return SERVICE_STATE_STOPPED;
    }
    ServiceInstance* instance = iterator->second;
    mutex_unlock(&instance_ledger.mutex);

    return service_instance_get_state(instance);
}

const ServiceManifest* service_manager_find_manifest(const char* id) {
    mutex_lock(&manifest_ledger.mutex);
    const auto iterator = manifest_ledger.manifests.find(id);
    const ServiceManifest* manifest = (iterator != manifest_ledger.manifests.end()) ? iterator->second : nullptr;
    mutex_unlock(&manifest_ledger.mutex);
    return manifest;
}

ServiceContext* service_manager_find_context(const char* id) {
    mutex_lock(&instance_ledger.mutex);
    const auto iterator = instance_ledger.instances.find(id);
    ServiceInstance* instance = (iterator != instance_ledger.instances.end()) ? iterator->second : nullptr;
    mutex_unlock(&instance_ledger.mutex);
    return instance;
}

} // extern "C"
