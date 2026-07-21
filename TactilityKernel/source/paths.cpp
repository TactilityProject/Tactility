// SPDX-License-Identifier: Apache-2.0

#include <tactility/paths.h>
#include <tactility/device.h>
#include <tactility/drivers/sdcard.h>
#include <tactility/filesystem/file_system.h>

#include <cstdio>
#include <cstring>

static error_t get_user_data_root_path(char* out_path, size_t out_path_size) {
#if defined(CONFIG_TT_USER_DATA_LOCATION_INTERNAL)
#ifdef ESP_PLATFORM
    const char* mount_point = "/data";
#else
    const char* mount_point = "data";
#endif
    if (std::strlen(mount_point) + 1 > out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    std::strcpy(out_path, mount_point);
    return ERROR_NONE;
#elif defined(CONFIG_TT_USER_DATA_LOCATION_SD)
    struct FileSystem* found = nullptr;
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

error_t paths_get_user_data_path(char* out_path, size_t out_path_size) {
#ifdef ESP_PLATFORM
    char root[64];
    error_t error = get_user_data_root_path(root, sizeof(root));
    if (error != ERROR_NONE) {
        return error;
    }
    int written = std::snprintf(out_path, out_path_size, "%s/tactility", root);
    if (written < 0 || (size_t)written >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
#else
    const char* fixed_path = "data";
    if (std::strlen(fixed_path) + 1 > out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    std::strcpy(out_path, fixed_path);
    return ERROR_NONE;
#endif
}

} // extern "C"
