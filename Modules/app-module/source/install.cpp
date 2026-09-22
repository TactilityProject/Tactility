// SPDX-License-Identifier: Apache-2.0
#include <app/install.h>

#include <app/manager.h>
#include <app/package_manifest.h>

#include <app/private/binary_path.h>
#include <app/private/fs.h>
#include <app/private/ledger.h>
#include <app/private/package_manifest_parsing.h>

#include <TactilityCpp/Allocator.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/log.h>
#include <tactility/paths.h>

#include <minitar.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

constexpr auto* TAG = "app_install";

namespace {

// region Filesystem helpers (app-module may not depend upward on Tactility::file; see
// app_metadata_parsing.cpp for the same constraint applied to properties-file loading)

std::string last_path_segment(const std::string& path) {
    auto index = path.find_last_of('/');
    return index == std::string::npos ? path : path.substr(index + 1);
}

// Rejects ".." components, so a crafted tar entry can't extract outside destination_path (CWE-22).
bool is_tar_entry_path_safe(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        size_t length = (slash == std::string::npos ? path.size() : slash) - start;
        if (path.compare(start, length, "..") == 0) {
            return false;
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }

    return true;
}

// mkdir -p.
bool ensure_directory(const std::string& path) {
    if (path.empty() || app_fs_is_directory(path)) {
        return true;
    }

    bool created = mkdir(path.c_str(), 0777) == 0 || errno == EEXIST;
    if (!created) {
        return false;
    }

    return app_fs_is_directory(path);
}

bool ensure_directory_recursive(const std::string& path) {
    for (size_t index = path.find('/', 1); index != std::string::npos; index = path.find('/', index + 1)) {
        if (!ensure_directory(path.substr(0, index))) {
            return false;
        }
    }
    return ensure_directory(path);
}

bool delete_recursively(const std::string& path) {
    LOG_I(TAG, "Deleting %s...", path.c_str());
    return app_fs_delete_recursively(path);
}

bool get_app_install_directory(std::string& out_path) {
    char root[FILE_MAX_PATH_STRING_LENGTH];
    if (paths_get_data_path(root, sizeof(root)) != ERROR_NONE) {
        return false;
    }
    out_path = std::string(root) + "/app";
    return true;
}

// endregion

// region Tar extraction (ported from the old Tactility::app AppInstall.cpp)

bool untar_file(minitar* archive, const minitar_entry* entry, const std::string& destination_path) {
    auto absolute_path = destination_path + "/" + entry->metadata.path;
    auto parent_path = absolute_path.substr(0, absolute_path.find_last_of('/'));
    if (!ensure_directory_recursive(parent_path)) {
        LOG_E(TAG, "Can't find or create directory %s", parent_path.c_str());
        return false;
    }

    if (!minitar_read_contents_to_file(archive, entry, absolute_path.c_str())) {
        LOG_E(TAG, "Failed to write data to %s", absolute_path.c_str());
        return false;
    }

    // Note: fchmod() doesn't exist on ESP-IDF and chmod() does nothing on that platform.
    chmod(absolute_path.c_str(), entry->metadata.mode);

    return true;
}

bool untar_directory(const minitar_entry* entry, const std::string& destination_path) {
    return ensure_directory_recursive(destination_path + "/" + entry->metadata.path);
}

bool untar(const std::string& tar_path, const std::string& destination_path) {
    minitar archive {};
    if (minitar_open(tar_path.c_str(), &archive) != 0) {
        LOG_E(TAG, "Failed to open %s", tar_path.c_str());
        return false;
    }

    bool success = true;
    minitar_entry entry {};
    while (minitar_read_entry(&archive, &entry) == 0) {
        LOG_I(TAG, "Extracting %s", entry.metadata.path);
        if (!is_tar_entry_path_safe(entry.metadata.path)) {
            LOG_E(TAG, "Rejecting unsafe tar entry path: %s", entry.metadata.path);
            success = false;
            break;
        }
        if (entry.metadata.type == MTAR_DIRECTORY) {
            if (std::strcmp(entry.metadata.name, ".") == 0 || std::strcmp(entry.metadata.name, "..") == 0 || std::strcmp(entry.metadata.name, "/") == 0) {
                continue;
            }
            success = untar_directory(&entry, destination_path);
        } else if (entry.metadata.type == MTAR_REGULAR) {
            success = untar_file(&archive, &entry, destination_path);
        } else {
            LOG_E(TAG, "Unsupported entry type: %d", static_cast<int>(entry.metadata.type));
            success = false;
        }

        if (!success) {
            LOG_E(TAG, "Failed to extract %s", entry.metadata.path);
            break;
        }
    }

    minitar_close(&archive);
    return success;
}

// endregion

// region Staging-path lock: at most one caller may clean up/populate a given staging_path at a
// time, keyed by source basename. The HTTP server and app tasks can call app_install_package()
// concurrently, e.g. two uploads sharing a source basename; without this lock, one call's
// cleanup could delete or overwrite the staging directory another call is still extracting into.

struct StagingLock {
    Mutex mutex {};
    int refcount = 0;
};

struct StagingLockTable {
    std::unordered_map<std::string, std::unique_ptr<StagingLock>> locks;
    Mutex table_mutex {};

    StagingLockTable() { mutex_construct(&table_mutex); }
};

StagingLockTable& staging_lock_table() {
    static StagingLockTable table;
    return table;
}

// Blocks until any other caller staging @a path has released it, then locks it for this caller.
// Must be paired with exactly one release_staging_lock(path) call.
void acquire_staging_lock(const std::string& path) {
    auto& table = staging_lock_table();
    mutex_lock(&table.table_mutex);
    auto iterator = table.locks.find(path);
    if (iterator == table.locks.end()) {
        auto lock = std::make_unique<StagingLock>();
        mutex_construct(&lock->mutex);
        iterator = table.locks.emplace(path, std::move(lock)).first;
    }
    StagingLock* lock = iterator->second.get();
    lock->refcount++;
    mutex_unlock(&table.table_mutex);

    mutex_lock(&lock->mutex);
}

// Erases the table entry once nothing references it anymore, so the table doesn't grow forever
// across installs with distinct basenames (e.g. unique upload temp names).
void release_staging_lock(const std::string& path) {
    auto& table = staging_lock_table();
    mutex_lock(&table.table_mutex);
    auto iterator = table.locks.find(path);
    if (iterator == table.locks.end()) {
        mutex_unlock(&table.table_mutex);
        return;
    }
    StagingLock* lock = iterator->second.get();
    mutex_unlock(&lock->mutex);
    if (--lock->refcount == 0) {
        table.locks.erase(iterator);
    }
    mutex_unlock(&table.table_mutex);
}

// endregion

// region Installed-package registry: owns the AppManifests (and their backing location-path
// strings) that app_manager's ledger only keeps non-owning pointers to (see app_manager_add()'s
// contract). AppManifest::id/name own their own storage directly (fixed arrays), so this doesn't
// need to separately own those.

struct InstalledPackageRecord {
    std::string path;                     // install directory
    std::vector<AppManifest> manifests;   // one per AppManifest the package declared
    std::vector<std::string> locations;   // backs manifests[i].location.location, same indices
};

struct InstallRegistry {
    std::unordered_map<std::string, std::unique_ptr<InstalledPackageRecord>> apps;
    Mutex mutex {};

    InstallRegistry() { mutex_construct(&mutex); }
};

InstallRegistry& install_registry() {
    static InstallRegistry registry;
    return registry;
}

// Registers every AppManifest in @a bindings (@a count of them) with app_manager_add(), taking
// ownership of their backing location strings, then registers @a package itself.
// @warning Caller must hold install_registry().mutex.
error_t register_installed_package_locked(const PackageManifest& package, const std::string& install_path, const AppManifestBinding* bindings, size_t count) {
    auto& registry = install_registry();

    auto record = std::make_unique<InstalledPackageRecord>();
    record->path = install_path;
    // Sized once, up front: manifests[i].location.location points into locations[i].c_str(),
    // which would dangle if either vector reallocated afterward.
    record->manifests.resize(count);
    record->locations.resize(count);

    for (size_t i = 0; i < count; i++) {
        record->manifests[i] = bindings[i].manifest;
        record->locations[i] = app_resolve_binary_path(install_path, bindings[i].binary);
        record->manifests[i].location = { APP_LOCATION_PATH, const_cast<char*>(record->locations[i].c_str()) };
    }

    for (size_t i = 0; i < count; i++) {
        // The caller's earlier uninstall_locked() call is meant to have already cleared any stale registration for this id
        // (e.g. left over from app_manager_install_path_scan()'s separate registry),
        // but that call happens before the package is even extracted / the binaries moved into place; remove once more
        // right before add, so a duplicate id can never turn a filesystem-level install success into a reported failure.
        // Only if installed (APP_LOCATION_PATH) - never steal a built-in's id.
        AppManifest existing {};
        if (app_manager_find_manifest(record->manifests[i].id, &existing) == ERROR_NONE && existing.location.type == APP_LOCATION_PATH) {
            app_manager_remove(record->manifests[i].id);
        }

        error_t add_result = app_manager_add(&record->manifests[i]);
        if (add_result != ERROR_NONE) {
            LOG_E(TAG, "Failed to register app '%s': %s", record->manifests[i].id, error_to_string(add_result));
            // All-or-nothing: unregister whatever this package already added before failing.
            for (size_t j = 0; j < i; j++) {
                app_manager_remove(record->manifests[j].id);
            }
            return add_result;
        }
    }

    std::vector<const char*> app_id_ptrs;
    app_id_ptrs.reserve(count);
    for (size_t i = 0; i < count; i++) {
        app_id_ptrs.push_back(record->manifests[i].id);
    }
    app_manager_remove_package(package.id);
    error_t add_package_result = app_manager_add_package(&package, app_id_ptrs.data(), app_id_ptrs.size());
    if (add_package_result != ERROR_NONE) {
        LOG_E(TAG, "Failed to register package '%s': %s", package.id, error_to_string(add_package_result));
        for (size_t i = 0; i < count; i++) {
            app_manager_remove(record->manifests[i].id);
        }
        return add_package_result;
    }

    registry.apps[package.id] = std::move(record);
    return ERROR_NONE;
}

// Stops every currently-running instance of @a manifest. Collects matching instance ids while
// holding the ledger lock, then calls app_manager_stop() on each after releasing it: that call
// bound-joins the instance's thread, which must not happen while the ledger mutex (also taken by
// the instance's own thread_main()) is held, or the two threads would deadlock each other.
void stop_all_instances_of(const AppManifest* manifest) {
    std::vector<uint32_t> instance_ids;

    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    for (const auto& [id, record]: ledger.instances) {
        if (record.manifest == manifest) {
            instance_ids.push_back(id);
        }
    }
    mutex_unlock(&ledger.mutex);

    for (uint32_t id: instance_ids) {
        app_manager_stop(id);
    }
}

// Caller must already hold install_registry().mutex
error_t uninstall_locked(const std::string& package_id) {
    auto& registry = install_registry();
    auto iterator = registry.apps.find(package_id);
    if (iterator == registry.apps.end()) {
        return ERROR_NOT_FOUND;
    }

    for (const auto& manifest : iterator->second->manifests) {
        // Can't uninstall in-memory apps
        if (manifest.location.type != APP_LOCATION_PATH) {
            continue;
        }
        stop_all_instances_of(&manifest);
        app_manager_remove(manifest.id);
    }
    app_manager_remove_package(package_id.c_str());
    delete_recursively(iterator->second->path);
    registry.apps.erase(iterator);

    return ERROR_NONE;
}

// endregion

} // namespace

extern "C" {

error_t app_get_install_path(const char* app_id, char* path, size_t path_size) {
    if (path_size == 0) {
        return ERROR_BUFFER_OVERFLOW;
    }
    path[0] = '\0';

    std::string app_parent_path;
    if (!get_app_install_directory(app_parent_path)) {
        return ERROR_NOT_FOUND;
    }

    int written = std::snprintf(path, path_size, "%s/%s", app_parent_path.c_str(), app_id);
    if (written < 0 || static_cast<size_t>(written) >= path_size) {
        path[0] = '\0';
        return ERROR_BUFFER_OVERFLOW;
    }

    return ERROR_NONE;
}

error_t app_install(const char* source_path) {
    LOG_I(TAG, "Installing app package from %s", source_path);

    std::string app_parent_path;
    if (!get_app_install_directory(app_parent_path)) {
        return ERROR_NOT_FOUND;
    }

    if (!ensure_directory_recursive(app_parent_path)) {
        LOG_E(TAG, "Failed to create %s", app_parent_path.c_str());
        return ERROR_NOT_FOUND;
    }

    auto source_name = last_path_segment(source_path);
    if (source_name.empty() || source_name == "." || source_name == "..") {
        LOG_E(TAG, "Invalid source path %s", source_path);
        return ERROR_INVALID_ARGUMENT;
    }
    auto staging_path = app_parent_path + "/" + source_name;
    acquire_staging_lock(staging_path);

    delete_recursively(staging_path);

    if (!untar(source_path, staging_path)) {
        LOG_E(TAG, "Failed to extract %s", source_path);
        delete_recursively(staging_path);
        release_staging_lock(staging_path);
        return ERROR_NOT_FOUND;
    }

    auto manifest_path = staging_path + "/manifest.properties";
    if (!app_fs_is_file(manifest_path)) {
        LOG_E(TAG, "Manifest not found at %s", manifest_path.c_str());
        delete_recursively(staging_path);
        release_staging_lock(staging_path);
        return ERROR_INVALID_ARGUMENT;
    }

    PackageManifest package {};
    std::vector<AppManifestBinding, tt::OptExternalAllocator<AppManifestBinding>> app_bindings;
    if (app_package_manifest_parse_into(manifest_path.c_str(), package, app_bindings) != ERROR_NONE) {
        LOG_E(TAG, "Install failed: invalid manifest");
        delete_recursively(staging_path);
        release_staging_lock(staging_path);
        return ERROR_INVALID_ARGUMENT;
    }

    auto& registry = install_registry();
    mutex_lock(&registry.mutex);

    // Replace any previous installation of this package - this also handles the app_manager
    // registrations of its old AppManifests, so there's no separate app_manager_remove() needed
    // here (register_installed_package_locked() below still defends against a stale registration
    // per individual id, e.g. one left by app_manager_install_path_scan()'s separate registry).
    uninstall_locked(package.id);

    auto final_path = app_parent_path + "/" + package.id;
    delete_recursively(final_path);

    if (rename(staging_path.c_str(), final_path.c_str()) != 0) {
        LOG_E(TAG, "Failed to rename \"%s\" to \"%s\"", staging_path.c_str(), final_path.c_str());
        delete_recursively(staging_path);
        release_staging_lock(staging_path);
        mutex_unlock(&registry.mutex);
        return ERROR_NOT_FOUND;
    }
    release_staging_lock(staging_path);

    // app_bindings.size(), not package.app_manifest_count - the safe bound to index by.
    error_t add_result = register_installed_package_locked(package, final_path, app_bindings.data(), app_bindings.size());
    mutex_unlock(&registry.mutex);

    return add_result;
}

error_t app_uninstall(const char* app_id) {
    LOG_I(TAG, "Uninstalling app %s", app_id);

    auto& registry = install_registry();
    mutex_lock(&registry.mutex);
    error_t error = uninstall_locked(app_id);
    mutex_unlock(&registry.mutex);

    if (error == ERROR_NOT_FOUND) {
        error = app_manager_install_path_uninstall(app_id);
    }

    if (error == ERROR_NONE) {
        LOG_I(TAG, "Uninstalled %s", app_id);
    } else {
        LOG_I(TAG, "Uninstalling %s failed: %s", app_id, error_to_string(error));
    }

    return error;
}

} // extern "C"
