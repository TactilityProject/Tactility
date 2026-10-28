// SPDX-License-Identifier: Apache-2.0
#include <mesh/mesh.h>
#include <mesh/mesh_service.h>
#include <mesh/module.h>

#include <app/manager.h>
#include <service/manager.h>

#include <tactility/error.h>
#include <tactility/module.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/log.h>
#include <tactility/system_event.h>

#include <atomic>

constexpr auto* TAG = "mesh";

// Kept across module restarts, as the boot event is only emitted once
static std::atomic<bool> boot_completed = false;

struct LifecycleMutex {
    Mutex handle {};
    LifecycleMutex() { mutex_construct(&handle); }
    ~LifecycleMutex() { mutex_destruct(&handle); }
};

// Serializes the boot callback with starting and stopping the module. A callback that was already
// dispatched can still run after the subscription was removed, so it checks module_active under the lock.
static LifecycleMutex lifecycle_mutex;
static bool module_active = false;

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
    mutex_lock(&lifecycle_mutex.handle);
    if (module_active) {
        start_service_if_enabled();
    }
    mutex_unlock(&lifecycle_mutex.handle);
}

static error_t start() {
    mutex_lock(&lifecycle_mutex.handle);
    error_t error = app_manager_add(&mesh_command_manifest);
    if (error != ERROR_NONE) {
        mutex_unlock(&lifecycle_mutex.handle);
        return error;
    }
    error = service_manager_add(&mesh_service_manifest, false);
    if (error != ERROR_NONE) {
        app_manager_remove(mesh_command_manifest.id);
        mutex_unlock(&lifecycle_mutex.handle);
        return error;
    }
    error = system_event_callback_add(KERNEL_EVENT_BOOT_COMPLETED, on_boot_completed, nullptr);
    if (error != ERROR_NONE) {
        service_manager_remove(mesh_service_manifest.id);
        app_manager_remove(mesh_command_manifest.id);
        mutex_unlock(&lifecycle_mutex.handle);
        return error;
    }
    module_active = true;
    // A restart after boot misses the event
    if (boot_completed) {
        start_service_if_enabled();
    }
    mutex_unlock(&lifecycle_mutex.handle);
    return ERROR_NONE;
}

static error_t stop() {
    system_event_callback_remove(KERNEL_EVENT_BOOT_COMPLETED, on_boot_completed);

    // Waits for a callback that is running, and makes one that is still pending do nothing
    mutex_lock(&lifecycle_mutex.handle);
    module_active = false;
    error_t error = service_manager_stop(mesh_service_manifest.id);
    if (error == ERROR_NONE || error == ERROR_NOT_FOUND) {
        error = service_manager_remove(mesh_service_manifest.id);
    }
    if (error == ERROR_NONE) {
        error = app_manager_remove(mesh_command_manifest.id);
    }
    mutex_unlock(&lifecycle_mutex.handle);
    return error;
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
