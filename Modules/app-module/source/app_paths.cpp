// SPDX-License-Identifier: Apache-2.0

#include <app/paths.h>
#include <tactility/paths.h>

#include <cstdio>

extern "C" {

error_t app_paths_get_user_data_directory(const char* app_id, char* out_path, size_t out_path_size) {
    char root[192];
    error_t error = paths_get_user_data_path(root, sizeof(root));
    if (error != ERROR_NONE) {
        return error;
    }
    int written = std::snprintf(out_path, out_path_size, "%s/app/%s", root, app_id);
    if (written < 0 || (size_t)written >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
}

error_t app_paths_get_user_data_path(const char* app_id, const char* child_path, char* out_path, size_t out_path_size) {
    char directory[224];
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
    char directory[224];
    error_t error = app_paths_get_user_data_directory(app_id, directory, sizeof(directory));
    if (error != ERROR_NONE) {
        return error;
    }
    int written = std::snprintf(out_path, out_path_size, "%s/assets", directory);
    if (written < 0 || (size_t)written >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
}

error_t app_paths_get_assets_path(const char* app_id, const char* child_path, char* out_path, size_t out_path_size) {
    char directory[224];
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
