// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Directory operations. All paths are absolute and already resolved, see fs.h. */

/** True if @a path exists and is a directory. */
bool directory_exists(const char* path);

/** One entry passed to directory_list()'s callback. */
struct DirectoryEntry {
    const char* name;
    bool is_directory;
    size_t size;
};

/**
 * @brief Lists a directory's entries ("." and ".." excluded).
 * @param[in] path the directory to list
 * @param[in] context passed through to @a callback
 * @param[in] callback invoked once per entry
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND the directory could not be opened
 */
error_t directory_list(const char* path, void* context, void (*callback)(const struct DirectoryEntry* entry, void* context));

/**
 * @brief Creates a directory.
 * @param[in] path the directory to create
 * @param[in] create_parents also create missing intermediate directories
 * @retval ERROR_NONE on success
 * @retval ERROR_ALREADY_EXISTS @a path already exists
 * @retval ERROR_RESOURCE an I/O error occurred
 */
error_t directory_make(const char* path, bool create_parents);

/**
 * @brief Deletes an empty directory.
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND @a path doesn't exist
 * @retval ERROR_NOT_EMPTY @a path is not empty
 * @retval ERROR_RESOURCE an I/O error occurred
 */
error_t directory_remove(const char* path);

/**
 * @brief Recursively deletes a directory and everything under it (or a plain file at @a path).
 * @warning Depth is bounded. A directory tree deeper than that is left partially deleted rather
 * than overflowing the stack.
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND @a path doesn't exist
 * @retval ERROR_NOT_EMPTY @a path contains an entry that could not be removed
 * @retval ERROR_RESOURCE an I/O error occurred
 */
error_t directory_remove_tree(const char* path);

#ifdef __cplusplus
}
#endif
