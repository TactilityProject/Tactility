// SPDX-License-Identifier: Apache-2.0
#include <app/execute.h>

#include <app/loader.h>
#include <app/location.h>
#include <app/private/manager_internal.h>

#include <service/instance.h>
#include <service/manager.h>

namespace {

// Same lookup as scheduler.cpp's own (private) find_loader_api() - duplicated rather than
// shared since it's a handful of lines and neither file depends on the other.
const char* loader_service_id_for(AppLocationType type) {
    return (type == APP_LOCATION_MEMORY) ? APP_LOADER_MEMORY_SERVICE_ID : APP_LOADER_PATH_SERVICE_ID;
}

const AppLoaderApi* find_loader_api(AppLocationType type) {
    ServiceInstance* instance = service_manager_find_instance(loader_service_id_for(type));
    if (instance == nullptr) {
        return nullptr;
    }
    return static_cast<const AppLoaderApi*>(service_instance_get_data(instance));
}

} // namespace

extern "C" {

error_t app_execute(AppLocation location, AppStackConfig stack, int argc, const char* const argv[], AppInstanceId* out_app_instance_id) {
    return app_manager_start_internal(nullptr, location, stack, 0, argc, argv, nullptr, 0, out_app_instance_id);
}

error_t app_execute_for_result(AppLocation location, AppStackConfig stack, int argc, const char* const argv[], AppInstanceId parent_instance_id, AppInstanceId* out_app_instance_id) {
    return app_manager_start_internal(nullptr, location, stack, parent_instance_id, argc, argv, nullptr, 0, out_app_instance_id);
}

error_t app_execute_with_streams(AppLocation location, AppStackConfig stack, int argc, const char* const argv[], const AppStreamBinding* bindings, size_t binding_count, AppInstanceId* out_app_instance_id) {
    return app_manager_start_internal(nullptr, location, stack, 0, argc, argv, bindings, binding_count, out_app_instance_id);
}

error_t app_execute_for_result_with_streams(AppLocation location, AppStackConfig stack, int argc, const char* const argv[], const AppStreamBinding* bindings, size_t binding_count, AppInstanceId parent_instance_id, AppInstanceId* out_app_instance_id) {
    return app_manager_start_internal(nullptr, location, stack, parent_instance_id, argc, argv, bindings, binding_count, out_app_instance_id);
}

bool app_is_executable_path(const char* path) {
    const AppLoaderApi* loader = find_loader_api(APP_LOCATION_PATH);
    if (loader == nullptr || loader->is_executable == nullptr) {
        return false;
    }
    AppLocation location = {
        .type = APP_LOCATION_PATH,
        .location = (void*)path,
    };
    return loader->is_executable(location);
}

bool app_is_executable(AppLocation location) {
    const AppLoaderApi* loader = find_loader_api(location.type);
    if (loader == nullptr || loader->is_executable == nullptr) {
        return false;
    }
    return loader->is_executable(location);
}

} // extern "C"
