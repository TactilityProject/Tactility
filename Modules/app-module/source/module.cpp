// SPDX-License-Identifier: Apache-2.0
#include <app/dir.h>
#include <app/env.h>
#include <app/event.h>
#include <app/execute.h>
#include <app/install.h>
#include <app/io.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/memory.h>
#include <app/package_manifest.h>
#include <app/paths.h>
#include <app/scheduler.h>
#include <app/signal.h>
#include <app/start.h>
#include <app/stream.h>

#include <service/manager.h>

#include <tactility/error.h>
#include <tactility/filesystem/fs.h>
#include <tactility/log.h>
#include <tactility/module.h>
#include <tactility/paths.h>

#include <cstdio>
#include <initializer_list>

// Deprecated app_execute*/app_start* symbols stay exported for apps built against older SDKs
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

constexpr auto* TAG = "app-module";

extern "C" {

extern ServiceManifest app_internal_loader_service_manifest;

static const ModuleSymbol SYMBOLS[] = {
    // app/dir
    DEFINE_MODULE_SYMBOL(app_dir_get_cwd),
    DEFINE_MODULE_SYMBOL(app_dir_set_cwd),
    // app/env
    DEFINE_MODULE_SYMBOL(app_env_set),
    DEFINE_MODULE_SYMBOL(app_env_unset),
    DEFINE_MODULE_SYMBOL(app_env_get),
    DEFINE_MODULE_SYMBOL(app_env_put),
    // app/event
    DEFINE_MODULE_SYMBOL(app_event_subscribe),
    DEFINE_MODULE_SYMBOL(app_event_subscribe_with_app_id),
    DEFINE_MODULE_SYMBOL(app_event_unsubscribe),
    DEFINE_MODULE_SYMBOL(app_event_poll),
    DEFINE_MODULE_SYMBOL(app_event_emit_close),
    // app/execute
    DEFINE_MODULE_SYMBOL(app_execute),
    DEFINE_MODULE_SYMBOL(app_execute_for_result),
    DEFINE_MODULE_SYMBOL(app_execute_with_streams),
    DEFINE_MODULE_SYMBOL(app_execute_for_result_with_streams),
    DEFINE_MODULE_SYMBOL(app_is_executable),
    DEFINE_MODULE_SYMBOL(app_is_executable_path),
    // app/install
    DEFINE_MODULE_SYMBOL(app_get_install_path),
    DEFINE_MODULE_SYMBOL(app_install),
    DEFINE_MODULE_SYMBOL(app_uninstall),
    DEFINE_MODULE_SYMBOL(app_module_for_each),
    DEFINE_MODULE_SYMBOL(app_module_set_started),
    // app/io
    DEFINE_MODULE_SYMBOL(app_io_read),
    DEFINE_MODULE_SYMBOL(app_io_write),
    DEFINE_MODULE_SYMBOL(app_io_close),
    DEFINE_MODULE_SYMBOL(app_io_await),
    DEFINE_MODULE_SYMBOL(app_io_poll),
    DEFINE_MODULE_SYMBOL(app_io_ioctl),
    DEFINE_MODULE_SYMBOL(app_io_bind_self),
    // app/manager
    DEFINE_MODULE_SYMBOL(app_manager_stop),
    DEFINE_MODULE_SYMBOL(app_manager_get_state),
    DEFINE_MODULE_SYMBOL(app_manager_find_manifest),
    DEFINE_MODULE_SYMBOL(app_manager_for_each_manifest),
    DEFINE_MODULE_SYMBOL(app_manager_add),
    DEFINE_MODULE_SYMBOL(app_manager_remove),
    DEFINE_MODULE_SYMBOL(app_manager_add_package),
    DEFINE_MODULE_SYMBOL(app_manager_remove_package),
    DEFINE_MODULE_SYMBOL(app_manager_add_package_listener),
    DEFINE_MODULE_SYMBOL(app_manager_remove_package_listener),
    DEFINE_MODULE_SYMBOL(app_manager_find_package),
    DEFINE_MODULE_SYMBOL(app_manager_for_each_package),
    DEFINE_MODULE_SYMBOL(app_manager_get_topmost_instance_id),
    DEFINE_MODULE_SYMBOL(app_manager_get_topmost_app_id),
    DEFINE_MODULE_SYMBOL(app_manager_install_path_add),
    DEFINE_MODULE_SYMBOL(app_manager_install_path_scan),
    DEFINE_MODULE_SYMBOL(app_manager_install_path_uninstall),
    // app/manifest
    DEFINE_MODULE_SYMBOL(app_manifest_id_is_valid),
    DEFINE_MODULE_SYMBOL(app_manifest_name_is_valid),
    DEFINE_MODULE_SYMBOL(app_manifest_stack_size_is_valid),
    // app/memory
    DEFINE_MODULE_SYMBOL(app_memory_record_alloc),
    DEFINE_MODULE_SYMBOL(app_memory_record_free),
    // app/package_manifest
    DEFINE_MODULE_SYMBOL(app_package_manifest_parse),
    DEFINE_MODULE_SYMBOL(app_package_manifest_is_compatible),
    // app/paths
    DEFINE_MODULE_SYMBOL(app_paths_get_user_data_directory),
    DEFINE_MODULE_SYMBOL(app_paths_get_user_data_path),
    DEFINE_MODULE_SYMBOL(app_paths_get_assets_directory),
    DEFINE_MODULE_SYMBOL(app_paths_get_assets_path),
    // app/scheduler
    DEFINE_MODULE_SYMBOL(app_scheduler_current_app_id),
    // app/signal
    DEFINE_MODULE_SYMBOL(app_signal_send),
    // app/start
    DEFINE_MODULE_SYMBOL(app_start_context_for_manifest),
    DEFINE_MODULE_SYMBOL(app_start_context_for_location),
    DEFINE_MODULE_SYMBOL(app_start_context_from_id),
    DEFINE_MODULE_SYMBOL(app_start_context_set_stack),
    DEFINE_MODULE_SYMBOL(app_start_context_set_arguments_ext),
    DEFINE_MODULE_SYMBOL(app_start_context_set_arguments),
    DEFINE_MODULE_SYMBOL(app_start_context_set_streams),
    DEFINE_MODULE_SYMBOL(app_start_context_set_parent),
    DEFINE_MODULE_SYMBOL(app_start_context_set_environment),
    DEFINE_MODULE_SYMBOL(app_start_with_context),
    DEFINE_MODULE_SYMBOL(app_start),
    DEFINE_MODULE_SYMBOL(app_start_for_result),
    DEFINE_MODULE_SYMBOL(app_start_with_streams),
    DEFINE_MODULE_SYMBOL(app_start_for_result_with_streams),
    // app/stream
    DEFINE_MODULE_SYMBOL(app_stream_bind_alias_fd),
    DEFINE_MODULE_SYMBOL(app_stream_subscribe),
    DEFINE_MODULE_SYMBOL(app_stream_unsubscribe),
    DEFINE_MODULE_SYMBOL(app_stream_await),
    DEFINE_MODULE_SYMBOL(app_stream_read),
    DEFINE_MODULE_SYMBOL(app_stream_write),
    DEFINE_MODULE_SYMBOL(app_stream_close),
    // terminator
    MODULE_SYMBOL_TERMINATOR,
};

static void ensure_directory(const char* data_path, const char* name) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (snprintf(path, sizeof(path), "%s/%s", data_path, name) >= static_cast<int>(sizeof(path))) {
        LOG_E(TAG, "Path too long for %s/%s", data_path, name);
        return;
    }
    const error_t error = directory_make(path, true);
    if (error != ERROR_NONE && error != ERROR_ALREADY_EXISTS) {
        LOG_E(TAG, "Failed to create %s", path);
    }
}

// Apps' data used to live in <data>/user/app
static void migrate_legacy_appdata(const char* data_path) {
    char legacy_path[FILE_MAX_PATH_STRING_LENGTH];
    char appdata_path[FILE_MAX_PATH_STRING_LENGTH];
    if (snprintf(legacy_path, sizeof(legacy_path), "%s/user/app", data_path) >= static_cast<int>(sizeof(legacy_path)) ||
        snprintf(appdata_path, sizeof(appdata_path), "%s/appdata", data_path) >= static_cast<int>(sizeof(appdata_path))) {
        return;
    }
    if (!directory_exists(legacy_path) || path_exists(appdata_path)) {
        return;
    }
    LOG_I(TAG, "Migrating %s to %s", legacy_path, appdata_path);
    const error_t error = file_move(legacy_path, appdata_path, false);
    if (error == ERROR_NONE) {
        LOG_I(TAG, "Migrated %s to %s", legacy_path, appdata_path);
        char user_path[FILE_MAX_PATH_STRING_LENGTH];
        if (snprintf(user_path, sizeof(user_path), "%s/user", data_path) < static_cast<int>(sizeof(user_path)) &&
            directory_remove(user_path) == ERROR_NONE) {
            LOG_I(TAG, "Removed empty %s", user_path);
        }
    } else {
        LOG_E(TAG, "Failed to migrate %s to %s (error %d)", legacy_path, appdata_path, static_cast<int>(error));
    }
}

static error_t start() {
    char data_path[FILE_MAX_PATH_STRING_LENGTH];
    if (paths_get_data_path(data_path, sizeof(data_path)) != ERROR_NONE) {
        LOG_E(TAG, "Failed to determine data path");
        return ERROR_RESOURCE;
    }
    migrate_legacy_appdata(data_path);
    for (const char* name : { "app", "appdata" }) {
        ensure_directory(data_path, name);
    }

    return service_manager_add(&app_internal_loader_service_manifest, /*auto_start=*/true);
}

static error_t stop() {
    return service_manager_remove(app_internal_loader_service_manifest.id);
}

Module app_module = {
    .name = "app",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = SYMBOLS,
    .internal = nullptr,
};

}
