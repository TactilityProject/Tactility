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
};

extern "C" {

error_t service_instance_construct(ServiceInstance* instance, const ServiceManifest* manifest) {
    instance->internal = new(std::nothrow) ServiceInstanceInternal;
    if (instance->internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    mutex_construct(&instance->internal->mutex);

    instance->manifest = manifest;
    instance->service = manifest->create_service(manifest->context);

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

    instance->manifest->destroy_service(instance->service, instance->manifest->context);
    instance->service = nullptr;

    instance->internal = nullptr;
    mutex_destruct(&internal->mutex);
    delete internal;

    return ERROR_NONE;
}

const ServiceManifest* service_instance_get_manifest(ServiceInstance* instance) {
    return instance->manifest;
}

Service* service_instance_get_service(ServiceInstance* instance) {
    return instance->service;
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

} // extern "C"
