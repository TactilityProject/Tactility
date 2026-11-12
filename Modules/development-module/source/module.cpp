// SPDX-License-Identifier: Apache-2.0
#include <development/module.h>
#include <development/remote_access.h>
#include <development/server.h>
#include <development/session.h>

#include <app/manager.h>
#include <service/manager.h>

#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

static error_t start() {
    development_remote_start();
    error_t error = app_manager_add(&development_session_app_manifest);
    if (error != ERROR_NONE) {
        return error;
    }
    error = app_manager_add(&development_remote_session_app_manifest);
    if (error != ERROR_NONE) {
        app_manager_remove(development_session_app_manifest.id);
        return error;
    }
    error = service_manager_add(&development_server_service_manifest, /*auto_start=*/true);
    if (error != ERROR_NONE) {
        app_manager_remove(development_remote_session_app_manifest.id);
        app_manager_remove(development_session_app_manifest.id);
        development_remote_stop();
    }
    return error;
}

static error_t stop() {
    const error_t error = service_manager_stop(development_server_service_manifest.id);
    if (error != ERROR_NONE && error != ERROR_NOT_FOUND) {
        return error;
    }
    service_manager_remove(development_server_service_manifest.id);
    app_manager_remove(development_remote_session_app_manifest.id);
    app_manager_remove(development_session_app_manifest.id);
    development_remote_stop();
    return ERROR_NONE;
}

static const ModuleSymbol development_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(development_server_set_enabled),
    DEFINE_MODULE_SYMBOL(development_server_is_enabled),
    DEFINE_MODULE_SYMBOL(development_server_get_fingerprint),
    DEFINE_MODULE_SYMBOL(development_server_get_port),
    DEFINE_MODULE_SYMBOL(development_server_for_each_trusted_client),
    DEFINE_MODULE_SYMBOL(development_server_remove_trusted_client),
    MODULE_SYMBOL_TERMINATOR
};

Module development_module = {
    .name = "development",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = development_module_symbols,
    .internal = nullptr,
};

}
