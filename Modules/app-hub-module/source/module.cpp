// SPDX-License-Identifier: Apache-2.0
#include <app_hub/app_hub.h>
#include <app_hub/app_hub_service.h>
#include <app_hub/module.h>

#include <app/manager.h>
#include <service/manager.h>

#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

static error_t start() {
    error_t error = app_manager_add(&app_hub_command_manifest);
    if (error != ERROR_NONE) {
        return error;
    }
    error = service_manager_add(&app_hub_service_manifest, /*auto_start=*/true);
    if (error != ERROR_NONE) {
        app_manager_remove(app_hub_command_manifest.id);
    }
    return error;
}

static error_t stop() {
    error_t error = service_manager_stop(app_hub_service_manifest.id);
    if (error != ERROR_NONE && error != ERROR_NOT_FOUND) {
        return error;
    }
    error = service_manager_remove(app_hub_service_manifest.id);
    if (error != ERROR_NONE) {
        return error;
    }
    return app_manager_remove(app_hub_command_manifest.id);
}

static const ModuleSymbol app_hub_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(app_hub_search),
    DEFINE_MODULE_SYMBOL(app_hub_get_app),
    DEFINE_MODULE_SYMBOL(app_hub_get_details),
    DEFINE_MODULE_SYMBOL(app_hub_get_icon),
    DEFINE_MODULE_SYMBOL(app_hub_is_busy),
    DEFINE_MODULE_SYMBOL(app_hub_refresh),
    DEFINE_MODULE_SYMBOL(app_hub_install),
    DEFINE_MODULE_SYMBOL(app_hub_update_all),
    DEFINE_MODULE_SYMBOL(app_hub_remove),
    DEFINE_MODULE_SYMBOL(app_hub_repository_for_each),
    DEFINE_MODULE_SYMBOL(app_hub_repository_add),
    DEFINE_MODULE_SYMBOL(app_hub_repository_remove),
    DEFINE_MODULE_SYMBOL(app_hub_repository_set_enabled),
    DEFINE_MODULE_SYMBOL(app_hub_event_subscribe),
    DEFINE_MODULE_SYMBOL(app_hub_event_unsubscribe),
    DEFINE_MODULE_SYMBOL(app_hub_event_await),
    DEFINE_MODULE_SYMBOL(app_hub_event_poll),
    MODULE_SYMBOL_TERMINATOR
};

Module app_hub_module = {
    .name = "app-hub",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = app_hub_module_symbols,
    .internal = nullptr,
};

}
