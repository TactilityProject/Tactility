// SPDX-License-Identifier: Apache-2.0
#include <app/manager.h>
#include <app/package_manifest.h>
#include <app/private/env_internal.h>
#include <app/private/fd_table.h>
#include <app/private/fs.h>
#include <app/private/ledger.h>
#include <app/private/manager_internal.h>
#include <app/private/manifest_set.h>
#include <app/private/package_manifest_parsing.h>
#include <app/private/scheduler.h>

#include <TactilityCpp/Allocator.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <ranges>
#include <unordered_map>
#include <vector>

#define TAG "app_manager"

extern "C" {

error_t app_manager_add(const AppManifest* manifest) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    if (ledger.manifests.contains(manifest->id)) {
        mutex_unlock(&ledger.mutex);
        LOG_E(TAG, "Manifest with id '%s' is already registered", manifest->id);
        return ERROR_INVALID_ARGUMENT;
    }
    ledger.manifests[manifest->id] = manifest;
    mutex_unlock(&ledger.mutex);

    return ERROR_NONE;
}

error_t app_manager_remove(const char* id) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.manifests.find(id);
    if (iterator == ledger.manifests.end()) {
        mutex_unlock(&ledger.mutex);
        return ERROR_NOT_FOUND;
    }
    ledger.manifests.erase(iterator);
    mutex_unlock(&ledger.mutex);

    return ERROR_NONE;
}

error_t app_manager_find_manifest(const char* id, AppManifest* out_manifest) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.manifests.find(id);
    if (iterator == ledger.manifests.end()) {
        mutex_unlock(&ledger.mutex);
        return ERROR_NOT_FOUND;
    }
    *out_manifest = *iterator->second;
    mutex_unlock(&ledger.mutex);
    return ERROR_NONE;
}

void app_manager_for_each_manifest(AppManifestVisitorFn visitor, void* context) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    for (auto& manifest: ledger.manifests | std::views::values) {
        visitor(manifest, context);
    }
    mutex_unlock(&ledger.mutex);
}

error_t app_manager_add_package(const PackageManifest* package, const char* const* app_ids, size_t app_id_count) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    if (ledger.packages.contains(package->id)) {
        mutex_unlock(&ledger.mutex);
        LOG_E(TAG, "Package with id '%s' is already registered", package->id);
        return ERROR_INVALID_ARGUMENT;
    }
    AppPackageRecord record { .package = *package, .app_ids = {} };
    record.app_ids.reserve(app_id_count);
    for (size_t i = 0; i < app_id_count; i++) {
        record.app_ids.emplace_back(app_ids[i]);
    }
    ledger.packages[package->id] = std::move(record);
    mutex_unlock(&ledger.mutex);

    return ERROR_NONE;
}

error_t app_manager_remove_package(const char* package_id) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.packages.find(package_id);
    if (iterator == ledger.packages.end()) {
        mutex_unlock(&ledger.mutex);
        return ERROR_NOT_FOUND;
    }
    ledger.packages.erase(iterator);
    mutex_unlock(&ledger.mutex);

    return ERROR_NONE;
}

error_t app_manager_find_package(const char* package_id, PackageManifest* out_package) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.packages.find(package_id);
    if (iterator == ledger.packages.end()) {
        mutex_unlock(&ledger.mutex);
        return ERROR_NOT_FOUND;
    }
    *out_package = iterator->second.package;
    mutex_unlock(&ledger.mutex);
    return ERROR_NONE;
}

void app_manager_for_each_package(AppPackageVisitorFn visitor, void* context) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    for (auto& [id, record] : ledger.packages) {
        std::vector<const char*> app_id_ptrs;
        app_id_ptrs.reserve(record.app_ids.size());
        for (const auto& app_id : record.app_ids) {
            app_id_ptrs.push_back(app_id.c_str());
        }
        AppPackage pkg { .package = record.package, .app_id_count = app_id_ptrs.size(), .app_ids = app_id_ptrs.data() };
        visitor(&pkg, context);
    }
    mutex_unlock(&ledger.mutex);
}

error_t app_manager_start_internal(const AppStartContext* context, AppInstanceId* out_app_instance_id) {
    const AppStreamBinding* bindings = context->bindings;
    size_t binding_count = context->binding_count;
    AppInstanceId parent_instance_id = context->parent_id;

    if (binding_count != 0 && bindings == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto& ledger = app_ledger();

    mutex_lock(&ledger.mutex);
    AppInstanceId target_id = ledger.next_instance_id++;
    AppInstanceRecord record {
        .id = target_id,
        .manifestId = context->id,
        .state = APP_INSTANCE_STATE_STARTING,
        .task = nullptr,
    };
    record.parent_id = parent_instance_id;

    // Inherit the parent's environment and working directory
    if (parent_instance_id != 0) {
        auto parent_iterator = ledger.instances.find(parent_instance_id);
        if (parent_iterator != ledger.instances.end()) {
            record.env = parent_iterator->second.env;
            record.cwd = parent_iterator->second.cwd;
        }
    }
    app_env_apply(record.env, context->environment);
    ledger.instances[target_id] = record;
    // Construct on the map-resident copy, not `record`: fds[] point into slots[] by address
    // (fd_table.h), so constructing on the stack-local record would leave them dangling.
    app_fd_table_construct(&ledger.instances[target_id].fd_table);
    mutex_unlock(&ledger.mutex);

    LOG_I(TAG, "[instance %d] starting %s with parent %d", target_id, context->id[0] != '\0' ? context->id : "<unregistered>", parent_instance_id);

    for (size_t i = 0; i < binding_count; i++) {
        error_t bind_result = app_stream_subscribe(bindings[i].stream, bindings[i].buffer, bindings[i].buffer_capacity, bindings[i].event_group, target_id, bindings[i].producer_fd);
        if (bind_result == ERROR_NONE) {
            // Before app_scheduler_start() below creates the task, so the child can never
            // observe this stream's window size as unset (see AppStreamBinding::window_size).
            app_stream_set_window_size(bindings[i].stream, bindings[i].window_size.columns, bindings[i].window_size.rows);
            if (bindings[i].alias_fd >= 0) {
                bind_result = app_stream_bind_alias_fd(bindings[i].stream, bindings[i].alias_fd);
            }
        }
        if (bind_result != ERROR_NONE) {
            LOG_E(TAG, "[instance %d] Failed to bind stream at fd %d: %s", target_id, bindings[i].producer_fd, error_to_string(bind_result));
            // Undo bindings[0..i): teardown() below only closes the fd, not the event bits
            // or mutex app_stream_subscribe() claimed; only app_stream_unsubscribe() does.
            for (size_t j = 0; j < i; j++) {
                app_stream_unsubscribe(bindings[j].stream);
            }
            mutex_lock(&ledger.mutex);
            app_fd_table_teardown(&ledger.instances[target_id].fd_table);
            ledger.instances.erase(target_id);
            mutex_unlock(&ledger.mutex);
            return bind_result;
        }
    }

    error_t error = app_scheduler_start(target_id, context);
    if (error != ERROR_NONE) {
        for (size_t j = 0; j < binding_count; j++) {
            app_stream_unsubscribe(bindings[j].stream);
        }
        mutex_lock(&ledger.mutex);
        app_fd_table_teardown(&ledger.instances[target_id].fd_table);
        ledger.instances.erase(target_id);
        mutex_unlock(&ledger.mutex);
        LOG_I(TAG, "[instance %d] Failed to start: %s", target_id, error_to_string(error));
        return error;
    }

    *out_app_instance_id = target_id;
    return ERROR_NONE;
}

error_t app_manager_stop(AppInstanceId app_instance_id) {
    return app_scheduler_stop(app_instance_id, pdMS_TO_TICKS(2000));
}

AppInstanceState app_manager_get_state(AppInstanceId app_instance_id) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(app_instance_id);
    AppInstanceState state = (iterator != ledger.instances.end()) ? iterator->second.state : APP_INSTANCE_STATE_STOPPED;
    mutex_unlock(&ledger.mutex);
    return state;
}

error_t app_manager_get_topmost_instance_id(AppInstanceId* out_app_instance_id) {
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    AppInstanceId topmost_id = 0;
    for (auto& [instance_id, record] : ledger.instances) {
        // Ids increase monotonically, so the highest Active id is the most recent.
        if (record.state == APP_INSTANCE_STATE_ACTIVE && instance_id > topmost_id) {
            topmost_id = instance_id;
        }
    }
    mutex_unlock(&ledger.mutex);

    if (topmost_id == 0) {
        return ERROR_NOT_FOUND;
    }
    *out_app_instance_id = topmost_id;
    return ERROR_NONE;
}

error_t app_manager_get_topmost_app_id(char* buffer, size_t buffer_size) {
    if (buffer_size == 0) {
        return ERROR_BUFFER_OVERFLOW;
    }
    buffer[0] = '\0';

    AppInstanceId topmost_id = 0;
    error_t result = app_manager_get_topmost_instance_id(&topmost_id);
    if (result != ERROR_NONE) {
        return result;
    }

    // Copied out while still holding the lock, not just the pointer/length: the instance (and its
    // manifestId string) can be erased by a concurrent app_manager_stop() the moment this unlocks.
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(topmost_id);
    if (iterator == ledger.instances.end() || iterator->second.manifestId.empty()) {
        mutex_unlock(&ledger.mutex);
        return ERROR_NOT_FOUND;
    }
    const std::string& manifest_id = iterator->second.manifestId;
    if (manifest_id.size() >= buffer_size) {
        mutex_unlock(&ledger.mutex);
        return ERROR_BUFFER_OVERFLOW;
    }
    memcpy(buffer, manifest_id.c_str(), manifest_id.size() + 1);
    mutex_unlock(&ledger.mutex);
    return ERROR_NONE;
}

} // extern "C"

namespace {

// Owns the AppManifests that app_manager_add() only keeps non-owning pointers to.
// Separate from app_install.cpp's registry: scanning only adds/removes registrations,
// never touches disk or running instances.
struct ScannedPackageManifest {
    std::string path;   // scanned directory
    AppManifestSet owned;

    ScannedPackageManifest(std::string scanned_path, const AppManifestBinding* bindings, size_t count)
        : path(std::move(scanned_path)), owned(path, bindings, count) {}
};

struct InstallPathRegistry {
    std::vector<std::string> paths;
    std::unordered_map<std::string, std::unique_ptr<ScannedPackageManifest>> scanned;
    Mutex mutex {};

    InstallPathRegistry() { mutex_construct(&mutex); }
};

InstallPathRegistry& install_path_registry() {
    static InstallPathRegistry registry;
    return registry;
}

} // namespace

extern "C" {

error_t app_manager_install_path_add(const char* path) {
    auto& registry = install_path_registry();
    mutex_lock(&registry.mutex);
    if (std::ranges::find(registry.paths, path) == registry.paths.end()) {
        registry.paths.emplace_back(path);
    }
    mutex_unlock(&registry.mutex);
    return ERROR_NONE;
}

void app_manager_install_path_scan(void) {
    auto& registry = install_path_registry();

    mutex_lock(&registry.mutex);
    auto paths_copy = registry.paths;
    mutex_unlock(&registry.mutex);

    std::vector<std::string> found_app_dirs;
    for (const auto& root : paths_copy) {
        app_fs_list_direct_subdirectories(root, found_app_dirs);
    }

    // Snapshot once so the rest of the scan doesn't hold registry.mutex. Keeps each known
    // package's own manifest ids too, so a package whose directory has disappeared can have all
    // of its (possibly several) app_manager registrations removed below, not just one.
    struct KnownPackage {
        std::string path;
        std::vector<std::string> manifest_ids;
    };
    mutex_lock(&registry.mutex);
    std::unordered_map<std::string, KnownPackage> known_packages;
    for (const auto& [id, record] : registry.scanned) {
        KnownPackage known { .path = record->path, .manifest_ids = {} };
        for (const auto& manifest : record->owned.manifests) {
            known.manifest_ids.emplace_back(manifest.id);
        }
        known_packages.emplace(id, std::move(known));
    }
    mutex_unlock(&registry.mutex);

    // Parses without registry.mutex held; filesystem IO is slow.
    std::vector<std::unique_ptr<ScannedPackageManifest>> new_records;
    std::vector<std::string> new_package_ids;
    std::vector<PackageManifest> new_packages;
    for (const auto& app_dir : found_app_dirs) {
        auto manifest_path = app_dir + "/manifest.properties";
        if (!app_fs_is_file(manifest_path)) {
            continue;
        }

        PackageManifest package {};
        // Heap-allocated (not a stack array - too large for a typical app task's stack; this
        // function runs on whichever task calls app_manager_install_path_scan(), e.g. Boot's, via
        // registerInstalledAppsFromFileSystems()) and sized to fit exactly, not pre-allocated to
        // some fixed maximum (see app_package_manifest_parse_into()). OptExternalAllocator prefers
        // PSRAM for this transient buffer, freeing up scarce internal RAM.
        std::vector<AppManifestBinding, tt::OptExternalAllocator<AppManifestBinding>> app_bindings;
        if (app_package_manifest_parse_into(manifest_path.c_str(), package, app_bindings) != ERROR_NONE) {
            LOG_W(TAG, "Invalid manifest at %s", manifest_path.c_str());
            continue;
        }

        if (known_packages.contains(package.id)) {
            continue;
        }

        auto record = std::make_unique<ScannedPackageManifest>(app_dir, app_bindings.data(), app_bindings.size());
        new_package_ids.emplace_back(package.id);
        new_packages.push_back(package);
        new_records.push_back(std::move(record));
    }

    std::vector<std::string> missing_ids;
    for (const auto& [id, known] : known_packages) {
        if (!app_fs_is_directory(known.path)) {
            missing_ids.push_back(id);
        }
    }

    // app_manager_add()/remove() take the ledger mutex internally, so calling them under
    // registry.mutex would fix a lock order an opposite-order caller could deadlock against.
    // registry.mutex is retaken afterward only to publish the in-memory results.
    for (const auto& id : missing_ids) {
        for (const auto& manifest_id : known_packages.at(id).manifest_ids) {
            app_manager_remove(manifest_id.c_str());
        }
        app_manager_remove_package(id.c_str());
    }
    std::vector<std::unique_ptr<ScannedPackageManifest>> added_records;
    std::vector<std::string> added_package_ids;
    for (size_t r = 0; r < new_records.size(); r++) {
        auto& record = new_records[r];

        // known_packages is only a pre-scan snapshot - two new directories can still share an id.
        if (std::ranges::find(added_package_ids, new_package_ids[r]) != added_package_ids.end()) {
            LOG_W(TAG, "Skipping duplicate package id %s found in this scan", new_package_ids[r].c_str());
            continue;
        }

        auto& manifests = record->owned.manifests;

        size_t added_count = 0;
        for (; added_count < manifests.size(); added_count++) {
            if (app_manager_add(&manifests[added_count]) != ERROR_NONE) {
                LOG_E(TAG, "Failed to register app %s (duplicate id?)", manifests[added_count].id);
                break;
            }
        }
        if (added_count != manifests.size()) {
            // All-or-nothing: unregister whatever this package already added before failing.
            for (size_t j = 0; j < added_count; j++) {
                app_manager_remove(manifests[j].id);
            }
            continue;
        }

        std::vector<const char*> app_id_ptrs;
        app_id_ptrs.reserve(manifests.size());
        for (const auto& app_manifest : manifests) {
            app_id_ptrs.push_back(app_manifest.id);
        }
        if (app_manager_add_package(&new_packages[r], app_id_ptrs.data(), app_id_ptrs.size()) != ERROR_NONE) {
            LOG_E(TAG, "Failed to register package %s (duplicate id?)", new_packages[r].id);
            for (const auto& app_manifest : manifests) {
                app_manager_remove(app_manifest.id);
            }
            continue;
        }

        added_package_ids.push_back(new_package_ids[r]);
        added_records.push_back(std::move(record));
    }

    mutex_lock(&registry.mutex);
    for (const auto& id : missing_ids) {
        registry.scanned.erase(id);
    }
    for (size_t i = 0; i < added_records.size(); i++) {
        registry.scanned[added_package_ids[i]] = std::move(added_records[i]);
    }
    mutex_unlock(&registry.mutex);
}

error_t app_manager_install_path_uninstall(const char* app_id) {
    auto& registry = install_path_registry();

    mutex_lock(&registry.mutex);
    auto iterator = registry.scanned.find(app_id);
    if (iterator == registry.scanned.end()) {
        mutex_unlock(&registry.mutex);
        return ERROR_NOT_FOUND;
    }

    const auto* manifests = &iterator->second->owned.manifests;
    auto path = iterator->second->path;
    mutex_unlock(&registry.mutex);

    // Mirrors stop_all_instances_of() in app_install.cpp. Collect under ledger.mutex, stop
    // outside it: app_manager_stop() bound-joins the thread, which itself takes ledger.mutex.
    std::vector<uint32_t> instance_ids;
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    for (const auto& [id, record] : ledger.instances) {
        for (const auto& manifest : *manifests) {
            if (record.manifestId == manifest.id) {
                instance_ids.push_back(id);
                break;
            }
        }
    }
    mutex_unlock(&ledger.mutex);

    for (uint32_t id : instance_ids) {
        app_manager_stop(id);
    }

    // app_manager_remove() takes ledger.mutex; call outside registry.mutex too, matching
    // the lock order in app_manager_install_path_scan().
    for (const auto& manifest : *manifests) {
        app_manager_remove(manifest.id);
    }
    app_manager_remove_package(app_id);

    // Delete before erasing the scan record, so a failed deletion still leaves the
    // entry discoverable for a retry.
    if (!app_fs_delete_recursively(path)) {
        return ERROR_RESOURCE;
    }

    mutex_lock(&registry.mutex);
    registry.scanned.erase(app_id);
    mutex_unlock(&registry.mutex);

    return ERROR_NONE;
}

} // extern "C"
