// SPDX-License-Identifier: Apache-2.0
#include <app/manager.h>
#include <app/package_manifest.h>
#include <app/private/arguments.h>
#include <app/private/binary_path.h>
#include <app/private/fd_table.h>
#include <app/private/fs.h>
#include <app/private/ledger.h>
#include <app/private/manager_internal.h>
#include <app/private/package_manifest_parsing.h>
#include <app/private/scheduler.h>

#include <TactilityCpp/Allocator.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <algorithm>
#include <cstring>
#include <memory>
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
    for (auto& [id, manifest] : ledger.manifests) {
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

error_t app_manager_start_internal(const AppManifest* manifest, AppLocation location, AppStackConfig stack, AppInstanceId parent_instance_id, int argc, const char* const argv_in[], const AppStreamBinding* bindings, size_t binding_count, AppInstanceId* out_app_instance_id) {
    char** argv = app_arguments_copy(argc, argv_in);
    if (argc > 0 && argv == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    if (binding_count != 0 && bindings == nullptr) {
        app_arguments_free(argc, argv);
        return ERROR_INVALID_ARGUMENT;
    }

    auto& ledger = app_ledger();

    mutex_lock(&ledger.mutex);
    AppInstanceId target_id = ledger.next_instance_id++;
    AppInstanceRecord record { .id = target_id, .manifest = manifest, .state = APP_INSTANCE_STATE_STARTING, .task = nullptr };
    record.parent_id = parent_instance_id;
    ledger.instances[target_id] = record;
    // Construct on the map-resident copy, not `record`: fds[] point into slots[] by address
    // (fd_table.h), so constructing on the stack-local record would leave them dangling.
    app_fd_table_construct(&ledger.instances[target_id].fd_table);
    mutex_unlock(&ledger.mutex);

    LOG_I(TAG, "[instance %d] starting %s with parent %d", target_id, manifest != nullptr ? manifest->id : "<unregistered>", parent_instance_id);

    for (size_t i = 0; i < binding_count; i++) {
        error_t bind_result = app_stream_subscribe(bindings[i].stream, bindings[i].buffer, bindings[i].buffer_capacity, bindings[i].event_group, target_id, bindings[i].producer_fd);
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
            app_arguments_free(argc, argv);
            return bind_result;
        }
    }

    error_t error = app_scheduler_start(target_id, location, stack, argc, argv);
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

    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(topmost_id);
    const AppManifest* manifest = (iterator != ledger.instances.end()) ? iterator->second.manifest : nullptr;
    const char* app_id = manifest != nullptr ? manifest->id : nullptr;
    mutex_unlock(&ledger.mutex);

    if (app_id == nullptr) {
        return ERROR_NOT_FOUND;
    }

    size_t length = strlen(app_id);
    if (length >= buffer_size) {
        buffer[0] = '\0';
        return ERROR_BUFFER_OVERFLOW;
    }
    memcpy(buffer, app_id, length + 1);
    return ERROR_NONE;
}

} // extern "C"

namespace {

// Owns the AppManifests (and their backing location-path strings) that app_manager_add() only
// keeps non-owning pointers to. Separate from app_install.cpp's registry: scanning only
// adds/removes registrations, never touches disk or running instances. AppManifest::id/name own
// their own storage directly (fixed arrays), so this doesn't need to separately own those.
struct ScannedPackageManifest {
    std::string path;                     // scanned directory
    std::vector<AppManifest> manifests;   // one per AppManifest the package declared
    std::vector<std::string> locations;   // backs manifests[i].location.location, same indices
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
        for (const auto& manifest : record->manifests) {
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

        auto record = std::make_unique<ScannedPackageManifest>();
        record->path = app_dir;
        // Sized once, up front: manifests[i].location.location points into locations[i].c_str(),
        // which would dangle if either vector reallocated afterward.
        record->manifests.resize(app_bindings.size());
        record->locations.resize(app_bindings.size());
        for (size_t i = 0; i < app_bindings.size(); i++) {
            record->manifests[i] = app_bindings[i].manifest;
            record->locations[i] = app_resolve_binary_path(app_dir, app_bindings[i].binary);
            record->manifests[i].location = { APP_LOCATION_PATH, const_cast<char*>(record->locations[i].c_str()) };
        }
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

        size_t added_count = 0;
        for (; added_count < record->manifests.size(); added_count++) {
            if (app_manager_add(&record->manifests[added_count]) != ERROR_NONE) {
                LOG_E(TAG, "Failed to register app %s (duplicate id?)", record->manifests[added_count].id);
                break;
            }
        }
        if (added_count != record->manifests.size()) {
            // All-or-nothing: unregister whatever this package already added before failing.
            for (size_t j = 0; j < added_count; j++) {
                app_manager_remove(record->manifests[j].id);
            }
            continue;
        }

        std::vector<const char*> app_id_ptrs;
        app_id_ptrs.reserve(record->manifests.size());
        for (const auto& app_manifest : record->manifests) {
            app_id_ptrs.push_back(app_manifest.id);
        }
        if (app_manager_add_package(&new_packages[r], app_id_ptrs.data(), app_id_ptrs.size()) != ERROR_NONE) {
            LOG_E(TAG, "Failed to register package %s (duplicate id?)", new_packages[r].id);
            for (const auto& app_manifest : record->manifests) {
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

    // Pointer, not a copy: the ledger's own AppInstanceRecord::manifest pointers (set by
    // app_manager_add() from this exact vector) are compared against it by address below, same
    // as the pre-existing single-manifest version of this function did.
    const std::vector<AppManifest>* manifests = &iterator->second->manifests;
    auto path = iterator->second->path;
    mutex_unlock(&registry.mutex);

    // Mirrors stop_all_instances_of() in app_install.cpp. Collect under ledger.mutex, stop
    // outside it: app_manager_stop() bound-joins the thread, which itself takes ledger.mutex.
    std::vector<uint32_t> instance_ids;
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    for (const auto& [id, record] : ledger.instances) {
        for (const auto& manifest : *manifests) {
            if (record.manifest == &manifest) {
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
