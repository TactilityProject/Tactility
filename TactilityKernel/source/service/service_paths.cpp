// SPDX-License-Identifier: Apache-2.0

#include <tactility/service/service_paths.h>
#include <tactility/paths.h>

#include <cstdio>

extern "C" {

error_t service_paths_get_user_data_directory(const char* service_id, char* out_path, size_t out_path_size) {
    char root[192];
    error_t error = paths_get_user_data_path(root, sizeof(root));
    if (error != ERROR_NONE) {
        return error;
    }
    int written = std::snprintf(out_path, out_path_size, "%s/service/%s", root, service_id);
    if (written < 0 || (size_t)written >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
}

error_t service_paths_get_user_data_path(const char* service_id, const char* child_path, char* out_path, size_t out_path_size) {
    char directory[224];
    error_t error = service_paths_get_user_data_directory(service_id, directory, sizeof(directory));
    if (error != ERROR_NONE) {
        return error;
    }
    int written = std::snprintf(out_path, out_path_size, "%s/%s", directory, child_path);
    if (written < 0 || (size_t)written >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
}

error_t service_paths_get_assets_directory(const char* service_id, char* out_path, size_t out_path_size) {
    char directory[224];
    error_t error = service_paths_get_user_data_directory(service_id, directory, sizeof(directory));
    if (error != ERROR_NONE) {
        return error;
    }
    int written = std::snprintf(out_path, out_path_size, "%s/assets", directory);
    if (written < 0 || (size_t)written >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
}

error_t service_paths_get_assets_path(const char* service_id, const char* child_path, char* out_path, size_t out_path_size) {
    char directory[224];
    error_t error = service_paths_get_assets_directory(service_id, directory, sizeof(directory));
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
