// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/filesystem/file_system.h>
#include <vector>

// Define the internal FileSystem structure
struct FileSystem {
    const FileSystemApi* api;
    void* data;
};

// Global list of file systems and its mutex
struct FileSystemsLedger {
    std::vector<FileSystem*> file_systems;
    // Use recursive mutex so that file_system_for_each() can lock within the callback
    RecursiveMutex mutex {};

    FileSystemsLedger() { recursive_mutex_construct(&mutex); }
    ~FileSystemsLedger() { recursive_mutex_destruct(&mutex); }

    void lock() { recursive_mutex_lock(&mutex); }
    bool is_locked() { return recursive_mutex_is_locked(&mutex); }
    void unlock() { recursive_mutex_unlock(&mutex); }
};

static FileSystemsLedger& get_ledger() {
    static FileSystemsLedger ledger;
    return ledger;
}

extern "C" {

FileSystem* file_system_add(const FileSystemApi* fs_api, void* data) {
    auto& ledger = get_ledger();
    check(!ledger.is_locked()); // ensure file_system_for_each() doesn't add a filesystem while iterating
    ledger.lock();
    
    auto* fs = new(std::nothrow) struct FileSystem();
    check(fs != nullptr);
    fs->api = fs_api;
    fs->data = data;
    ledger.file_systems.push_back(fs);
    
    ledger.unlock();
    return fs;
}

void file_system_remove(FileSystem* fs) {
    check(!file_system_is_mounted(fs));
    auto& ledger = get_ledger();
    check(!ledger.is_locked()); // ensure file_system_for_each() doesn't remove a filesystem while iterating
    ledger.lock();
    
    auto it = std::ranges::find(ledger.file_systems, fs);
    if (it != ledger.file_systems.end()) {
        ledger.file_systems.erase(it);
        delete fs;
    }
    
    ledger.unlock();
}

void file_system_for_each(void* callback_context, bool (*callback)(FileSystem* fs, void* context)) {
    auto& ledger = get_ledger();
    ledger.lock();
    for (auto* fs : ledger.file_systems) {
        if (!callback(fs, callback_context)) break;
    }
    ledger.unlock();
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
