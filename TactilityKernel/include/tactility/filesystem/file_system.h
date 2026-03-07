// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FileSystem;

/**
 * @brief File system API.
 * 
 * Provides a set of function pointers to interact with a specific file system implementation.
 */
struct FileSystemApi {
    /**
     * @brief Mounts the file system.
     * @param[in] data file system private data
     * @return ERROR_NONE on success, or an error code
     */
    error_t (*mount)(void* data);

    /**
     * @brief Unmounts the file system.
     * @param[in] data file system private data
     * @return ERROR_NONE on success, or an error code
     */
    error_t (*unmount)(void* data);

    /**
     * @brief Checks if the file system is mounted.
     * @param[in] data file system private data
     * @return true if mounted, false otherwise
     */
    bool (*is_mounted)(void* data);

    /**
     * @brief Gets the mount path.
     * @param[in] data file system private data
     * @param[out] out_path buffer to store the path
     * @param[in] out_path_size size of the output buffer
     * @return ERROR_NONE on success, or an error code
     */
    error_t (*get_path)(void* data, char* out_path, size_t out_path_size);
};

/**
 * @brief Registers a new file system.
 * @param[in] fs_api the file system API implementation
 * @param[in] data private data for the file system
 * @return the registered FileSystem object
 */
struct FileSystem* file_system_add(const struct FileSystemApi* fs_api, void* data);

/**
 * @brief Removes a registered file system.
 * @note The file system must be unmounted before removal.
 * @param[in] fs the FileSystem object to remove
 */
void file_system_remove(struct FileSystem* fs);

/**
 * @brief Iterates over all registered file systems.
 * @param[in] callback_context context passed to the callback
 * @param[in] callback function called for each file system. Return true to continue, false to stop.
 */
void file_system_for_each(void* callback_context, bool (*callback)(struct FileSystem* fs, void* context));

/**
 * @brief Mounts the file system.
 * @param[in] fs the FileSystem object
 * @return ERROR_NONE on success, or an error code
 */
error_t file_system_mount(struct FileSystem* fs);

/**
 * @warning Unmounting can fail (e.g. when the device is busy), so you might need to retry it.
 * @brief Unmounts the file system.
 * @param[in] fs the FileSystem object
 * @return ERROR_NONE on success, or an error code
 */
error_t file_system_unmount(struct FileSystem* fs);

/**
 * @brief Checks if the file system is mounted.
 * @param[in] fs the FileSystem object
 * @return true if mounted, false otherwise
 */
bool file_system_is_mounted(struct FileSystem* fs);

/**
 * @brief Gets the mount path.
 * @param[in] fs the FileSystem object
 * @param[out] out_path buffer to store the path
 * @param[in] out_path_size size of the output buffer
 * @return ERROR_NONE on success, or an error code
 */
error_t file_system_get_path(struct FileSystem* fs, char* out_path, size_t out_path_size);

#ifdef __cplusplus
}
#endif
