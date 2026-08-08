// SPDX-License-Identifier: Apache-2.0
#include <app_esp32/module.h>

#include <service/manager.h>

#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

extern ServiceManifest loader_service_manifest;

static error_t start() {
    return service_manager_add(&loader_service_manifest, /*auto_start=*/true);
}

static error_t stop() {
    return service_manager_remove(loader_service_manifest.id);
}

Module app_esp32_module = {
    .name = "app-esp32",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr
};

}
