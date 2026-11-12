// SPDX-License-Identifier: Apache-2.0
#include <bluetooth/module.h>
#include <bluetooth/private/bluetooth_service.h>

#include <service/manager.h>

#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

static error_t start() {
    return service_manager_add(&bluetooth_service_manifest, /*auto_start=*/true);
}

static error_t stop() {
    error_t error = service_manager_stop(bluetooth_service_manifest.id);
    if (error != ERROR_NONE && error != ERROR_NOT_FOUND) {
        return error;
    }
    return service_manager_remove(bluetooth_service_manifest.id);
}

Module bluetooth_module = {
    .name = "bluetooth",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr,
};

}
