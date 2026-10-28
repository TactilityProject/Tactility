// SPDX-License-Identifier: Apache-2.0
#include <mesh/mesh.h>
#include <mesh/mesh_service.h>
#include <mesh/module.h>

#include <app/manager.h>
#include <service/manager.h>

#include <tactility/error.h>
#include <tactility/module.h>
#include <tactility/log.h>
#include <tactility/system_event.h>

#include <atomic>

constexpr auto* TAG = "mesh";

// Kept across module restarts, as the boot event is only emitted once
static std::atomic<bool> boot_completed = false;

extern "C" {

// Radios can start after this module does, so the service only starts once all devices are up
static void start_service_if_enabled() {
    if (mesh_service_should_auto_start() && service_manager_start(mesh_service_manifest.id) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start the service");
    }
}

static void on_boot_completed(SystemEvent* event, void* context) {
    (void)event;
    (void)context;
    boot_completed = true;
    start_service_if_enabled();
}

static error_t start() {
    error_t error = app_manager_add(&mesh_command_manifest);
    if (error != ERROR_NONE) {
        return error;
    }
    error = service_manager_add(&mesh_service_manifest, false);
    if (error != ERROR_NONE) {
        app_manager_remove(mesh_command_manifest.id);
        return error;
    }
    error = system_event_callback_add(KERNEL_EVENT_BOOT_COMPLETED, on_boot_completed, nullptr);
    if (error != ERROR_NONE) {
        service_manager_remove(mesh_service_manifest.id);
        app_manager_remove(mesh_command_manifest.id);
        return error;
    }
    // A restart after boot misses the event
    if (boot_completed) {
        start_service_if_enabled();
    }
    return ERROR_NONE;
}

static error_t stop() {
    system_event_callback_remove(KERNEL_EVENT_BOOT_COMPLETED, on_boot_completed);
    error_t error = service_manager_stop(mesh_service_manifest.id);
    if (error != ERROR_NONE && error != ERROR_NOT_FOUND) {
        return error;
    }
    error = service_manager_remove(mesh_service_manifest.id);
    if (error != ERROR_NONE) {
        return error;
    }
    return app_manager_remove(mesh_command_manifest.id);
}

static const ModuleSymbol mesh_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(mesh_set_enabled),
    DEFINE_MODULE_SYMBOL(mesh_is_enabled),
    DEFINE_MODULE_SYMBOL(mesh_get_state),
    DEFINE_MODULE_SYMBOL(mesh_state_to_string),
    DEFINE_MODULE_SYMBOL(mesh_set_node_name),
    DEFINE_MODULE_SYMBOL(mesh_get_node_name),
    DEFINE_MODULE_SYMBOL(mesh_is_available),
    DEFINE_MODULE_SYMBOL(mesh_get_radio_count),
    DEFINE_MODULE_SYMBOL(mesh_get_radio_name),
    DEFINE_MODULE_SYMBOL(mesh_get_radio_kind),
    DEFINE_MODULE_SYMBOL(mesh_set_radio),
    DEFINE_MODULE_SYMBOL(mesh_get_radio),
    DEFINE_MODULE_SYMBOL(mesh_get_backend_count),
    DEFINE_MODULE_SYMBOL(mesh_get_backend_id),
    DEFINE_MODULE_SYMBOL(mesh_set_backend),
    DEFINE_MODULE_SYMBOL(mesh_get_backend),
    DEFINE_MODULE_SYMBOL(mesh_get_preset_count),
    DEFINE_MODULE_SYMBOL(mesh_get_preset_name),
    DEFINE_MODULE_SYMBOL(mesh_set_preset),
    DEFINE_MODULE_SYMBOL(mesh_get_preset),
    DEFINE_MODULE_SYMBOL(mesh_send_text),
    DEFINE_MODULE_SYMBOL(mesh_cancel),
    DEFINE_MODULE_SYMBOL(mesh_raw_send),
    DEFINE_MODULE_SYMBOL(mesh_message_get),
    DEFINE_MODULE_SYMBOL(mesh_history_get),
    DEFINE_MODULE_SYMBOL(mesh_conversation_get),
    DEFINE_MODULE_SYMBOL(mesh_get_contacts),
    DEFINE_MODULE_SYMBOL(mesh_add_contact),
    DEFINE_MODULE_SYMBOL(mesh_remove_contact),
    DEFINE_MODULE_SYMBOL(mesh_get_contact_uri),
    DEFINE_MODULE_SYMBOL(mesh_get_nodes),
    DEFINE_MODULE_SYMBOL(mesh_get_channels),
    DEFINE_MODULE_SYMBOL(mesh_set_channel),
    DEFINE_MODULE_SYMBOL(mesh_remove_channel),
    DEFINE_MODULE_SYMBOL(mesh_event_subscribe),
    DEFINE_MODULE_SYMBOL(mesh_event_unsubscribe),
    DEFINE_MODULE_SYMBOL(mesh_event_poll),
    MODULE_SYMBOL_TERMINATOR
};

Module mesh_module = {
    .name = "mesh",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = mesh_module_symbols,
    .internal = nullptr,
};

}
