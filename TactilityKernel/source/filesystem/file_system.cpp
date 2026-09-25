// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstring>
#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/device.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/system_event.h>
#include <vector>

// Define the internal FileSystem structure
struct FileSystem {
    const FileSystemApi* api;
    void* data;
    Device* owner;
    const char* name;
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

FileSystem* file_system_add(const char* name, const FileSystemApi* fs_api, void* data) {
    auto& ledger = get_ledger();
    check(!ledger.is_locked()); // ensure file_system_for_each() doesn't add a filesystem while iterating
    ledger.lock();

    auto* fs = new(std::nothrow) struct FileSystem();
    check(fs != nullptr);
    fs->api = fs_api;
    fs->data = data;
    fs->owner = nullptr;
    fs->name = name;
    ledger.file_systems.push_back(fs);

    ledger.unlock();
    return fs;
}

FileSystem* file_system_find_by_name(const char* name) {
    auto& ledger = get_ledger();
    ledger.lock();
    FileSystem* found = nullptr;
    for (auto* fs : ledger.file_systems) {
        if (fs->name != nullptr && strcmp(fs->name, name) == 0) {
            found = fs;
            break;
        }
    }
    ledger.unlock();
    return found;
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

void file_system_for_each_mounted(void* callback_context, bool (*callback)(FileSystem* fs, void* context)) {
    auto& ledger = get_ledger();
    ledger.lock();
    for (auto* fs : ledger.file_systems) {
        if (!file_system_is_mounted(fs)) {
            continue;
        }
        if (!callback(fs, callback_context)) break;
    }
    ledger.unlock();
}

error_t file_system_mount(FileSystem* fs) {
    // Assuming 'device' is accessible or passed via a different mechanism
    // as it's required by the FileSystemApi signatures.
    auto result = fs->api->mount(fs->data);
    if (result == ERROR_NONE) {
        FileSystemMountedEvent mounted_event = { .file_system = fs };
        system_event_emit(KERNEL_EVENT_FILE_SYSTEM_MOUNTED, &mounted_event, sizeof(mounted_event));
    }
    return result;
}

error_t file_system_unmount(FileSystem* fs) {
    auto result = fs->api->unmount(fs->data);
    if (result == ERROR_NONE) {
        FileSystemUnmountedEvent unmounted_event = { .file_system = fs };
        system_event_emit(KERNEL_EVENT_FILE_SYSTEM_UNMOUNTED, &unmounted_event, sizeof(unmounted_event));
    }
    return result;
}

bool file_system_is_mounted(FileSystem* fs) {
    return fs->api->is_mounted(fs->data);
}

error_t file_system_get_path(FileSystem* fs, char* out_path, size_t out_path_size) {
    return fs->api->get_path(fs->data, out_path, out_path_size);
}

const char* file_system_get_name(FileSystem* fs) {
    return fs->name;
}

void file_system_set_owner(FileSystem* fs, Device* owner) {
    fs->owner = owner;
}

Device* file_system_get_owner(FileSystem* fs) {
    return fs->owner;
}

}
