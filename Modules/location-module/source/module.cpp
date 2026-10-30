// SPDX-License-Identifier: Apache-2.0
#include <location/gps_provider.h>
#include <location/ip_provider.h>
#include <location/location.h>
#include <location/location_service.h>
#include <location/module.h>

#include <service/manager.h>

#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

static error_t start() {
    error_t error = ip_provider_track_network();
    if (error != ERROR_NONE) {
        return error;
    }
    error = location_provider_register(&gps_location_provider);
    if (error != ERROR_NONE) {
        ip_provider_untrack_network();
        return error;
    }
    error = location_provider_register(&ip_location_provider);
    if (error != ERROR_NONE) {
        location_provider_unregister(&gps_location_provider);
        ip_provider_untrack_network();
        return error;
    }
    return service_manager_add(&location_service_manifest, location_is_enabled());
}

static error_t stop() {
    const error_t error = service_manager_stop(location_service_manifest.id);
    if (error != ERROR_NONE && error != ERROR_NOT_FOUND) {
        return error;
    }
    location_provider_unregister(&ip_location_provider);
    location_provider_unregister(&gps_location_provider);
    ip_provider_untrack_network();
    return service_manager_remove(location_service_manifest.id);
}

static const ModuleSymbol location_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(location_set_enabled),
    DEFINE_MODULE_SYMBOL(location_is_enabled),
    DEFINE_MODULE_SYMBOL(location_set_precision),
    DEFINE_MODULE_SYMBOL(location_get_precision),
    DEFINE_MODULE_SYMBOL(location_get),
    DEFINE_MODULE_SYMBOL(location_subscribe),
    DEFINE_MODULE_SYMBOL(location_unsubscribe),
    DEFINE_MODULE_SYMBOL(location_for_each_subscriber),
    DEFINE_MODULE_SYMBOL(location_is_in_use),
    DEFINE_MODULE_SYMBOL(location_lookup_time_zone),
    MODULE_SYMBOL_TERMINATOR
};

Module location_module = {
    .name = "location",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = location_module_symbols,
    .internal = nullptr,
};

}
