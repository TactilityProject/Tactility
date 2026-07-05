// SPDX-License-Identifier: Apache-2.0

#include <tactility/service/service_instance.h>
#include <tactility/service/service_context.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/log.h>

#include <new>

#define TAG "service_instance"

struct ServiceInstanceInternal {
    Mutex mutex {};
    ServiceState state = SERVICE_STATE_STOPPED;
    uint32_t use_count = 0;
};

extern "C" {

error_t service_instance_construct(ServiceInstance* instance, const ServiceManifest* manifest) {
    instance->internal = new(std::nothrow) ServiceInstanceInternal;
    if (instance->internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    mutex_construct(&instance->internal->mutex);

    instance->manifest = manifest;
    instance->data = nullptr;
    instance->on_start = nullptr;
    instance->on_stop = nullptr;
    manifest->create_service(instance, manifest->context);

    LOG_D(TAG, "construct %s", manifest->id);
    return ERROR_NONE;
}

error_t service_instance_destruct(ServiceInstance* instance) {
    auto* internal = instance->internal;

    mutex_lock(&internal->mutex);
    if (internal->state != SERVICE_STATE_STOPPED) {
        mutex_unlock(&internal->mutex);
        return ERROR_INVALID_STATE;
    }
    mutex_unlock(&internal->mutex);

    LOG_D(TAG, "destruct %s", instance->manifest->id);

    instance->manifest->destroy_service(instance, instance->manifest->context);
    instance->data = nullptr;
    instance->on_start = nullptr;
    instance->on_stop = nullptr;

    instance->internal = nullptr;
    mutex_destruct(&internal->mutex);
    delete internal;

    return ERROR_NONE;
}

const ServiceManifest* service_instance_get_manifest(ServiceInstance* instance) {
    return instance->manifest;
}

void* service_instance_get_data(ServiceInstance* instance) {
    return instance->data;
}

ServiceState service_instance_get_state(ServiceInstance* instance) {
    mutex_lock(&instance->internal->mutex);
    ServiceState state = instance->internal->state;
    mutex_unlock(&instance->internal->mutex);
    return state;
}

/** Internal-only: mutate the state of a service instance. Used by service_registration.cpp. */
void service_instance_set_state(ServiceInstance* instance, ServiceState state) {
    mutex_lock(&instance->internal->mutex);
    instance->internal->state = state;
    mutex_unlock(&instance->internal->mutex);
}

const ServiceManifest* service_context_get_manifest(ServiceContext* context) {
    return service_instance_get_manifest(context);
}

bool service_instance_try_get(struct ServiceInstance* instance) {
    mutex_lock(&instance->internal->mutex);
    bool acquired = instance->internal->state == SERVICE_STATE_STARTED;
    if (acquired) {
        instance->internal->use_count++;
    }
    mutex_unlock(&instance->internal->mutex);
    return acquired;
}

void service_instance_put(struct ServiceInstance* instance) {
    mutex_lock(&instance->internal->mutex);
    if (instance->internal->use_count > 0) {
        instance->internal->use_count--;
    }
    mutex_unlock(&instance->internal->mutex);
}

} // extern "C"
