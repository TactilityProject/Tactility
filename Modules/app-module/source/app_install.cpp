// SPDX-License-Identifier: Apache-2.0
#include <app/install.h>

#include <app/manager.h>
#include <app/metadata.h>

#include <app/private/app_fs.h>
#include <app/private/app_ledger.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/filesystem/file_mutex.h>
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

// region Filesystem helpers (app-module may not depend upward on Tactility::file - see
// app_metadata_parsing.cpp for the same constraint applied to properties-file loading)

std::string last_path_segment(const std::string& path) {
    auto index = path.find_last_of('/');
    return index == std::string::npos ? path : path.substr(index + 1);
}

// mkdir -p.
bool ensure_directory(const std::string& path) {
    if (path.empty() || app_fs_is_directory(path)) {
        return true;
    }

    FileMutex mutex {};
    file_mutex_get(&mutex, path.c_str());
    file_mutex_lock(&mutex);
    bool created = mkdir(path.c_str(), 0777) == 0 || errno == EEXIST;
    file_mutex_unlock(&mutex);
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
    LOG_D(TAG, "Deleting %s...", path.c_str());
    if (path.empty() || path == "/" || path == "." || path == "..") {
        return true;
    }

    if (app_fs_is_directory(path)) {
        LOG_D(TAG, "Deleting dir %s", path.c_str());

        FileMutex file_mutex;
        file_mutex_get(&file_mutex, path.c_str());
        file_mutex_lock(&file_mutex);

        DIR* dir = opendir(path.c_str());
        if (dir == nullptr) {
            LOG_E(TAG, "Failed to scan directory %s", path.c_str());
            file_mutex_unlock(&file_mutex);
            return false;
        }

        bool success = true;
        dirent* entry;
        while (success && (entry = readdir(dir)) != nullptr) {
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            success = delete_recursively(path + "/" + entry->d_name);
        }
        closedir(dir);

        if (!success) {
            file_mutex_unlock(&file_mutex);
            return false;
        }

        bool result = rmdir(path.c_str()) == 0;
        file_mutex_unlock(&file_mutex);
        return result;
    }

    if (app_fs_is_file(path)) {
        LOG_D(TAG, "Deleting file %s", path.c_str());
        FileMutex mutex {};
        file_mutex_get(&mutex, path.c_str());
        file_mutex_lock(&mutex);
        bool result = remove(path.c_str()) == 0;
        file_mutex_unlock(&mutex);
        return result;
    }

    LOG_D(TAG, "Deleting done");
    return true;
}

bool get_app_install_directory(std::string& out_path) {
    char root[192];
    if (paths_get_user_data_path(root, sizeof(root)) != ERROR_NONE) {
        return false;
    }
    out_path = std::string(root) + "/app";
    return true;
}

// endregion

// region Tar extraction (ported from the old Tactility::app AppInstall.cpp)

bool untar_file(minitar* archive, const minitar_entry* entry, const std::string& destination_path) {
    auto absolute_path = destination_path + "/" + entry->metadata.path;
    if (!ensure_directory_recursive(destination_path)) {
        LOG_E(TAG, "Can't find or create directory %s", destination_path.c_str());
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

// region Installed-app registry: owns the AppManifest (and its id/name/path strings) that
// app_manager's ledger only keeps a non-owning pointer to (see app_manager_add()'s contract).

struct InstalledAppRecord {
    std::string id;
    std::string name;
    std::string path;
    AppManifest manifest {};
};

struct InstallRegistry {
    std::unordered_map<std::string, std::unique_ptr<InstalledAppRecord>> apps;
    Mutex mutex {};

    InstallRegistry() { mutex_construct(&mutex); }
};

InstallRegistry& install_registry() {
    static InstallRegistry registry;
    return registry;
}

// Registers @a app_dir_path (already confirmed to hold a valid manifest.properties, parsed into
// @a metadata) with app_manager_add(), taking ownership of its id/name/path strings.
// @warning Caller must hold install_registry().mutex, and must have already ensured
// @a metadata.app_id isn't already registered (app_manager_add() rejects duplicates, but the
// InstalledAppRecord for the earlier registration would leak since this always inserts fresh).
error_t register_installed_app_locked(const std::string& app_dir_path, const AppMetadata& metadata) {
    auto& registry = install_registry();

    auto record = std::make_unique<InstalledAppRecord>();
    record->id = metadata.app_id;
    record->name = metadata.app_name;
    record->path = app_dir_path;
    record->manifest = AppManifest {
        .id = record->id.c_str(),
        .name = record->name.c_str(),
        .category = APP_CATEGORY_USER,
        .location = { APP_LOCATION_PATH, const_cast<char*>(record->path.c_str()) },
        .flags = 0,
    };

    error_t add_result = app_manager_add(&record->manifest);
    if (add_result != ERROR_NONE) {
        return add_result;
    }

    registry.apps[record->id] = std::move(record);
    return ERROR_NONE;
}

// Stops every currently-running instance of @a manifest. Collects matching instance ids while
// holding the ledger lock, then calls app_manager_stop() on each after releasing it - that call
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
error_t uninstall_locked(const std::string& app_id) {
    auto& registry = install_registry();
    auto iterator = registry.apps.find(app_id);
    if (iterator == registry.apps.end()) {
        return ERROR_NOT_FOUND;
    }

    stop_all_instances_of(&iterator->second->manifest);
    app_manager_remove(app_id.c_str());
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
    LOG_I(TAG, "Installing app from %s", source_path);

    std::string app_parent_path;
    if (!get_app_install_directory(app_parent_path)) {
        return ERROR_NOT_FOUND;
    }

    if (!ensure_directory_recursive(app_parent_path)) {
        LOG_E(TAG, "Failed to create %s", app_parent_path.c_str());
        return ERROR_NOT_FOUND;
    }

    // Extract to a staging directory named after the tarball first - the real app id (and so
    // the final directory name) is only known once the manifest inside it is parsed.
    auto staging_path = app_parent_path + "/" + last_path_segment(source_path);
    delete_recursively(staging_path);

    FileMutex target_mutex {};
    file_mutex_get(&target_mutex, app_parent_path.c_str());
    FileMutex source_mutex {};
    file_mutex_get(&source_mutex, source_path);

    file_mutex_lock(&target_mutex);
    file_mutex_lock(&source_mutex);
    bool untar_success = untar(source_path, staging_path);
    file_mutex_unlock(&source_mutex);
    file_mutex_unlock(&target_mutex);

    if (!untar_success) {
        LOG_E(TAG, "Failed to extract %s", source_path);
        delete_recursively(staging_path);
        return ERROR_NOT_FOUND;
    }

    auto manifest_path = staging_path + "/manifest.properties";
    if (!app_fs_is_file(manifest_path)) {
        LOG_E(TAG, "Manifest not found at %s", manifest_path.c_str());
        delete_recursively(staging_path);
        return ERROR_INVALID_ARGUMENT;
    }

    AppMetadata metadata {};
    if (app_metadata_parse(manifest_path.c_str(), &metadata) != ERROR_NONE) {
        LOG_E(TAG, "Install failed: invalid manifest");
        delete_recursively(staging_path);
        return ERROR_INVALID_ARGUMENT;
    }

    auto& registry = install_registry();
    mutex_lock(&registry.mutex);

    // Replace any previous install of this app id (mirrors the old install()'s "already
    // running/present" handling). uninstall_locked() only clears app_install.cpp's own
    // registry - the same app id may instead be registered by app_manager_install_path_scan()
    // (manager.cpp's separate registry, scanning this same directory tree), which
    // uninstall_locked() doesn't know about. Clear the app-manager registration unconditionally
    // too, or app_manager_add() below rejects the re-add as a duplicate.
    uninstall_locked(metadata.app_id);
    if (app_manager_remove(metadata.app_id) != ERROR_NONE) {
        LOG_E(TAG, "Install failed: failed to remove existing installation");
        mutex_unlock(&registry.mutex);
        delete_recursively(staging_path);
        return ERROR_RESOURCE;
    }

    auto final_path = app_parent_path + "/" + metadata.app_id;
    delete_recursively(final_path);

    file_mutex_lock(&target_mutex);
    bool rename_success = rename(staging_path.c_str(), final_path.c_str()) == 0;
    file_mutex_unlock(&target_mutex);

    if (!rename_success) {
        LOG_E(TAG, "Failed to rename \"%s\" to \"%s\"", staging_path.c_str(), final_path.c_str());
        delete_recursively(staging_path);
        mutex_unlock(&registry.mutex);
        return ERROR_NOT_FOUND;
    }

    // Only remaining failure mode is a duplicate id - can't happen, uninstall_locked() above
    // already removed any previous registration for this exact id.
    error_t add_result = register_installed_app_locked(final_path, metadata);
    mutex_unlock(&registry.mutex);

    return add_result;
}

error_t app_uninstall(const char* app_id) {
    LOG_I(TAG, "Uninstalling app %s", app_id);

    auto& registry = install_registry();
    mutex_lock(&registry.mutex);
    error_t result = uninstall_locked(app_id);
    mutex_unlock(&registry.mutex);

    return result;
}

} // extern "C"
