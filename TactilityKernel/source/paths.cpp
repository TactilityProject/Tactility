// SPDX-License-Identifier: Apache-2.0

#include <tactility/paths.h>
#include <tactility/device.h>
#include <tactility/drivers/sdcard.h>
#include <tactility/filesystem/file_system.h>

#include <cstdio>
#include <cstring>

static error_t paths_get_data_root_path(char* out_path, size_t out_path_size) {
#if defined(CONFIG_TT_USER_DATA_LOCATION_INTERNAL)
    // Registered by platform-esp32/platform-posix's own module start() (see file_system_add()'s
    // "data" name), rather than a literal here: on POSIX that filesystem's real path is resolved
    // once via realpath() at registration time, not recomputed against whatever the caller's own
    // notion of "current directory" (real or virtual) happens to be right now.
    FileSystem* found = file_system_find_by_name("data");
    if (found == nullptr) {
        return ERROR_NOT_FOUND;
    }
    return file_system_get_path(found, out_path, out_path_size);
#elif defined(CONFIG_TT_USER_DATA_LOCATION_SD)
    FileSystem* found = nullptr;
    file_system_for_each(&found, [](FileSystem* fs, void* context) {
        auto* owner = file_system_get_owner(fs);
        if (owner == nullptr || device_get_type(owner) != &SDCARD_TYPE) {
            return true;
        }
        *static_cast<FileSystem**>(context) = fs;
        return false;
    });
    if (found == nullptr) {
        return ERROR_NOT_FOUND;
    }
    return file_system_get_path(found, out_path, out_path_size);
#else
#error CONFIG_TT_USER_DATA_* not set or unsupported
#endif
}

extern "C" {

error_t paths_get_data_path(char* out_path, size_t out_path_size) {
    // Sized for a real, realpath()-resolved absolute path (see paths_get_data_root_path()'s
    // CONFIG_TT_USER_DATA_LOCATION_INTERNAL branch), not just the short literal this used to be.
    char root[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = paths_get_data_root_path(root, sizeof(root));
    if (error != ERROR_NONE) {
        return error;
    }
    int written = std::snprintf(out_path, out_path_size, "%s/tactility", root);
    if (written < 0 || (size_t)written >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
}

error_t paths_get_temp_path(char* out_path, size_t out_path_size) {
    char data_path[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = paths_get_data_root_path(data_path, sizeof(data_path));
    if (error != ERROR_NONE) {
        return error;
    }
    int written = std::snprintf(out_path, out_path_size, "%s/tactility/tmp", data_path);
    if (written < 0 || (size_t)written >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
}

} // extern "C"
