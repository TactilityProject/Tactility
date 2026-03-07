// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FileSystem;

struct FileSystemApi {
    error_t (*mount)(void* data);
    error_t (*unmount)(void* data);
    bool (*is_mounted)(void* data);
    error_t (*get_path)(void* data, char* out_path, size_t out_path_size);
};

struct FileSystem* file_system_add(const struct FileSystemApi* fs_api, void* data);

void file_system_remove(struct FileSystem* fs);

void file_system_for_each(void* callback_context, bool (*callback)(struct FileSystem* fs, void* context));

error_t file_system_mount(struct FileSystem* fs);

error_t file_system_unmount(struct FileSystem* fs);

bool file_system_is_mounted(struct FileSystem* fs);

error_t file_system_get_path(struct FileSystem* fs, char* out_path, size_t out_path_size);

#ifdef __cplusplus
}
#endif
