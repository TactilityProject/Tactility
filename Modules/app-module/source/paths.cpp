// SPDX-License-Identifier: Apache-2.0

#include <app/location.h>
#include <app/manager.h>
#include <app/paths.h>
#include <tactility/paths.h>

#include <cstdio>
#include <string>

namespace {

// manifest.location.location is the fully-resolved binary file path
// ({install_dir}/bin/<platform>/<binary>.{elf,so} - see app_resolve_binary_path()), not the
// install directory itself - strip that fixed 3-segment suffix to recover it.
bool app_get_install_dir_from_binary_path(const char* binary_path, std::string& out_install_dir) {
    std::string path = binary_path;
    for (int i = 0; i < 3; i++) {
        auto separator = path.find_last_of('/');
        if (separator == std::string::npos) {
            return false;
        }
        path = path.substr(0, separator);
    }
    out_install_dir = path;
    return true;
}

} // namespace

extern "C" {

error_t app_paths_get_user_data_directory(const char* app_id, char* out_path, size_t out_path_size) {
    char root[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = paths_get_data_path(root, sizeof(root));
    if (error != ERROR_NONE) {
        return error;
    }
    int written = std::snprintf(out_path, out_path_size, "%s/user/app/%s", root, app_id);
    if (written < 0 || (size_t)written >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
}

error_t app_paths_get_user_data_path(const char* app_id, const char* child_path, char* out_path, size_t out_path_size) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = app_paths_get_user_data_directory(app_id, directory, sizeof(directory));
    if (error != ERROR_NONE) {
        return error;
    }
    int written = std::snprintf(out_path, out_path_size, "%s/%s", directory, child_path);
    if (written < 0 || (size_t)written >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
}

error_t app_paths_get_assets_directory(const char* app_id, char* out_path, size_t out_path_size) {
    AppManifest manifest;
    error_t error = app_manager_find_manifest(app_id, &manifest);
    if (error != ERROR_NONE) {
        return error;
    }

    if (manifest.location.type != APP_LOCATION_PATH) {
        return ERROR_NOT_FOUND;
    }

    std::string install_dir;
    if (!app_get_install_dir_from_binary_path(static_cast<const char*>(manifest.location.location), install_dir)) {
        return ERROR_NOT_FOUND;
    }

    int written = std::snprintf(out_path, out_path_size, "%s/assets", install_dir.c_str());
    if (written < 0 || (size_t)written >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
}

error_t app_paths_get_assets_path(const char* app_id, const char* child_path, char* out_path, size_t out_path_size) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = app_paths_get_assets_directory(app_id, directory, sizeof(directory));
    if (error != ERROR_NONE) {
        return error;
    }
    int written = std::snprintf(out_path, out_path_size, "%s/%s", directory, child_path);
    if (written < 0 || (size_t)written >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
}

} // extern "C"
