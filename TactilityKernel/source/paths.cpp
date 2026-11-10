// SPDX-License-Identifier: Apache-2.0

#include <tactility/paths.h>
#include <tactility/filesystem/file_system.h>

#include <cstdio>
#include <cstring>

#include <sys/stat.h>

extern "C" {

error_t paths_get_root_path(char* out_path, size_t out_path_size) {
    if (strlen(CONFIG_TT_ROOT_PATH) >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    strcpy(out_path, CONFIG_TT_ROOT_PATH);
    return ERROR_NONE;
}

bool paths_is_root_mounted() {
    struct stat st;
    return stat(CONFIG_TT_ROOT_PATH, &st) == 0 && S_ISDIR(st.st_mode);
}

error_t paths_get_data_root_path(char* out_path, size_t out_path_size) {
    return paths_get_root_path(out_path, out_path_size);
}

bool paths_is_data_root_mounted() {
    return paths_is_root_mounted();
}

error_t paths_get_data_path(char* out_path, size_t out_path_size) {
    char root[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = paths_get_root_path(root, sizeof(root));
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
    error_t error = paths_get_root_path(data_path, sizeof(data_path));
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
