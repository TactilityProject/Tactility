// SPDX-License-Identifier: Apache-2.0
#include <wifi/module.h>
#include <wifi/private/wifi_service.h>

#include <app/manager.h>
#include <app/manifest.h>
#include <service/manager.h>

#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

static error_t start() {
    error_t error = app_manager_add(&wifi_command_manifest);
    if (error != ERROR_NONE) {
        return error;
    }
    error = service_manager_add(&wifi_service_manifest, /*auto_start=*/true);
    if (error != ERROR_NONE) {
        app_manager_remove(wifi_command_manifest.id);
    }
    return error;
}

static error_t stop() {
    error_t error = service_manager_stop(wifi_service_manifest.id);
    if (error != ERROR_NONE && error != ERROR_NOT_FOUND) {
        return error;
    }
    error = service_manager_remove(wifi_service_manifest.id);
    if (error != ERROR_NONE) {
        return error;
    }
    return app_manager_remove(wifi_command_manifest.id);
}

Module wifi_module = {
    .name = "wifi",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr,
};

}
