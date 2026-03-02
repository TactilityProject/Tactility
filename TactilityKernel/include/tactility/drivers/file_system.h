// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;

struct FileSystemApi {
    error_t (*mount)(struct Device* device, const char* mount_path);
    error_t (*unmount)(struct Device* device);
    bool (*is_mounted)(struct Device* device);
    error_t (*get_mount_path)(struct Device*, char* out_path, size_t out_path_size);
};

extern const struct DeviceType FILE_SYSTEM_TYPE;

error_t file_system_mount(struct Device* device);

error_t file_system_unmount(struct Device* device);

bool file_system_is_mounted(struct Device* device);

error_t file_system_get_mount_path(struct Device*, char* out_path);

#ifdef __cplusplus
}
#endif
