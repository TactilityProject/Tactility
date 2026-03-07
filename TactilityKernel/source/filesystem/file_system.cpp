// SPDX-License-Identifier: Apache-2.0

#include <tactility/filesystem/file_system.h>
#include <tactility/concurrent/mutex.h>
#include <vector>
#include <algorithm>

// Define the internal FileSystem structure
struct FileSystem {
    const FileSystemApi* api;
    void* data;
};

// Global Mutex and the master list of file systems
static Mutex fs_mutex;
static bool fs_mutex_initialized = false;
static std::vector<FileSystem*> file_systems;

static void ensure_mutex_initialized() {
    if (!fs_mutex_initialized) {
        mutex_construct(&fs_mutex);
        fs_mutex_initialized = true;
    }
}

extern "C" {

FileSystem* file_system_add(const FileSystemApi* fs_api, void* data) {
    ensure_mutex_initialized();
    mutex_lock(&fs_mutex);
    
    auto* fs = new(std::nothrow) struct FileSystem();
    check(fs != nullptr);
    fs->api = fs_api;
    fs->data = data;
    file_systems.push_back(fs);
    
    mutex_unlock(&fs_mutex);
    return fs;
}

void file_system_remove(FileSystem* fs) {
    check(!file_system_is_mounted(fs));
    ensure_mutex_initialized();
    mutex_lock(&fs_mutex);
    
    auto it = std::ranges::find(file_systems, fs);
    if (it != file_systems.end()) {
        file_systems.erase(it);
        delete fs;
    }
    
    mutex_unlock(&fs_mutex);
}

void file_system_for_each(void* callback_context, bool (*callback)(FileSystem* fs, void* context)) {
    ensure_mutex_initialized();
    mutex_lock(&fs_mutex);
    
    for (auto* fs : file_systems) {
        if (!callback(fs, callback_context)) break;
    }
    
    mutex_unlock(&fs_mutex);
}

error_t file_system_mount(FileSystem* fs) {
    // Assuming 'device' is accessible or passed via a different mechanism
    // as it's required by the FileSystemApi signatures.
    return fs->api->mount(fs->data);
}

error_t file_system_unmount(FileSystem* fs) {
    return fs->api->unmount(fs->data);
}

bool file_system_is_mounted(FileSystem* fs) {
    return fs->api->is_mounted(fs->data);
}

error_t file_system_get_path(FileSystem* fs, char* out_path, size_t out_path_size) {
    return fs->api->get_path(fs->data, out_path, out_path_size);
}

}
