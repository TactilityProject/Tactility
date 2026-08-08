// SPDX-License-Identifier: Apache-2.0
#include <app/loader.h>
#include <app/manifest.h>

#include <service/instance.h>
#include <service/manager.h>

namespace {

error_t api_load(AppLocation location, AppRuntime* out_runtime) {
    if (location.type != APP_LOCATION_MEMORY) {
        return ERROR_NOT_SUPPORTED;
    }

    *out_runtime = location.location;
    return ERROR_NONE;
}

int32_t api_run(AppRuntime runtime, uint32_t app_instance_id, int argc, char* argv[]) {
    auto entry = reinterpret_cast<AppMainFn>(runtime);
    return entry(app_instance_id, argc, argv);
}

void api_unload(AppRuntime /*unused*/) {
}

AppLoaderApi memory_loader_api = {
    .load = api_load,
    .run = api_run,
    .unload = api_unload,
};

void* create_service(const ServiceManifest*) {
    return &memory_loader_api;
}

void destroy_service(const ServiceManifest*, void*) {
}

} // namespace

ServiceManifest app_internal_loader_service_manifest = {
    .id = APP_LOADER_MEMORY_SERVICE_ID,
    .create_service = create_service,
    .destroy_service = destroy_service,
    .on_start = nullptr,
    .on_stop = nullptr,
};
