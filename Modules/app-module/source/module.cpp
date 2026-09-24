// SPDX-License-Identifier: Apache-2.0
#include <app/dir.h>
#include <app/env.h>
#include <app/event.h>
#include <app/execute.h>
#include <app/install.h>
#include <app/io.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/package_manifest.h>
#include <app/paths.h>
#include <app/scheduler.h>
#include <app/start.h>
#include <app/stream.h>

#include <service/manager.h>

#include <tactility/error.h>
#include <tactility/module.h>

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
    // app/io
    DEFINE_MODULE_SYMBOL(app_io_read),
    DEFINE_MODULE_SYMBOL(app_io_write),
    DEFINE_MODULE_SYMBOL(app_io_close),
    DEFINE_MODULE_SYMBOL(app_io_await),
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
    // app/package_manifest
    DEFINE_MODULE_SYMBOL(app_package_manifest_parse),
    // app/paths
    DEFINE_MODULE_SYMBOL(app_paths_get_user_data_directory),
    DEFINE_MODULE_SYMBOL(app_paths_get_user_data_path),
    DEFINE_MODULE_SYMBOL(app_paths_get_assets_directory),
    DEFINE_MODULE_SYMBOL(app_paths_get_assets_path),
    // app/scheduler
    DEFINE_MODULE_SYMBOL(app_scheduler_current_app_id),
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

static error_t start() {
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
