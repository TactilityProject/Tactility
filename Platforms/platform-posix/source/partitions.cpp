// SPDX-License-Identifier: Apache-2.0
#ifndef ESP_PLATFORM

#include <tactility/error.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/log.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>

namespace {

constexpr auto* TAG = "platform-posix";

// A plain host directory has no real mount/unmount step to perform.
// mounted" here just tracks whether file_system_remove()'s precondition has been satisfied.
struct DirectoryFsData {
    char path[PATH_MAX];
    bool mounted;
};

DirectoryFsData system_fs_data;
DirectoryFsData data_fs_data;
DirectoryFsData root_fs_data;

error_t mount(void* data) {
    static_cast<DirectoryFsData*>(data)->mounted = true;
    return ERROR_NONE;
}

error_t unmount(void* data) {
    static_cast<DirectoryFsData*>(data)->mounted = false;
    return ERROR_NONE;
}

bool is_mounted(void* data) {
    return static_cast<DirectoryFsData*>(data)->mounted;
}

error_t get_path(void* data, char* out_path, size_t out_path_size) {
    auto* fs_data = static_cast<DirectoryFsData*>(data);
    if (strlen(fs_data->path) >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    strcpy(out_path, fs_data->path);
    return ERROR_NONE;
}

const FileSystemApi directory_fs_api = {
    .mount = mount,
    .unmount = unmount,
    .is_mounted = is_mounted,
    .get_path = get_path,
};

// relative_path is resolved against the process' current working directory.
// The simulator is expected to run with Data/ as its working directory, so "data"/"system" here match the real on-disk folder names.
// realpath() converts it to a real absolute path once, up front.
// Callers elsewhere (e.g. the shell) that ask for this filesystem's path via file_system_get_path() then
// get something meaningful regardless of their own notion of "current directory", real or virtual.
FileSystem* registerDirectoryFs(const char* name, const char* relativePath, DirectoryFsData* outData) {
    if (realpath(relativePath, outData->path) == nullptr) {
        LOG_E(TAG, "Failed to resolve '%s' to an absolute path: %s", relativePath, strerror(errno));
        return nullptr;
    }
    outData->mounted = true;
    return file_system_add(name, &directory_fs_api, outData);
}

} // namespace

error_t platform_posix_start_partitions() {
    registerDirectoryFs("system", "system", &system_fs_data);
    registerDirectoryFs("data", "data", &data_fs_data);
    // "data"/"system" live somewhere under here (wherever the process was launched from). Unlike
    // ESP32, POSIX genuinely has a real "/", so it gets registered too - discoverable the same
    // way as any other named filesystem, e.g. via `df`/file_system_for_each().
    registerDirectoryFs("root", "/", &root_fs_data);
    return ERROR_NONE;
}

#endif // ESP_PLATFORM
