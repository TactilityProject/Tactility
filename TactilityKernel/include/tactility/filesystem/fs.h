// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Path-level filesystem operations, all taking an already-resolved absolute path.
 *
 * Distinct from file_system.h, which is the mount-level API (registering/mounting a
 * FileSystem). These wrap the platform's own POSIX-style calls directly. ESP-IDF's VFS makes
 * that work unmodified on-device, same as the simulator.
 */

/** True if @a path exists, file or directory. */
bool path_exists(const char* path);

/** True if @a path exists and is a directory. */
bool directory_exists(const char* path);

/**
 * @brief Reads a whole file into a caller-owned buffer.
 * @param[in] path the file to read
 * @param[out] bytes buffer to fill
 * @param[in,out] out_size in: @a bytes's capacity. out: the number of bytes read
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND the file doesn't exist / couldn't be opened
 * @retval ERROR_BUFFER_OVERFLOW the file is larger than @a out_size's incoming capacity
 */
error_t file_read_binary(const char* path, uint8_t* bytes, size_t* out_size);

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
 * @brief Deletes a file.
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND @a path doesn't exist
 * @retval ERROR_RESOURCE an I/O error occurred
 */
error_t file_remove(const char* path);

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

/**
 * @brief Copies a file, streaming rather than buffering the whole thing.
 * @param[in] overwrite when false, fails with ERROR_ALREADY_EXISTS instead of overwriting @a target
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND @a source doesn't exist
 * @retval ERROR_ALREADY_EXISTS @a target exists and @a overwrite is false
 * @retval ERROR_RESOURCE an I/O error occurred
 */
error_t file_copy(const char* source, const char* target, bool overwrite);

/**
 * @brief Renames or moves a file. Falls back to copy-then-delete when crossing mount points.
 * @param[in] overwrite when false, fails with ERROR_ALREADY_EXISTS instead of overwriting @a target
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND @a source doesn't exist
 * @retval ERROR_ALREADY_EXISTS @a target exists and @a overwrite is false
 * @retval ERROR_RESOURCE an I/O error occurred
 */
error_t file_move(const char* source, const char* target, bool overwrite);

/**
 * @brief Creates an empty file, or does nothing if it already exists.
 * @retval ERROR_NONE on success
 * @retval ERROR_RESOURCE an I/O error occurred
 */
error_t file_touch(const char* path);

/**
 * @brief Total size in bytes of a file, or of everything under a directory.
 * @return the size, or 0 if @a path doesn't exist
 */
uint64_t path_tree_size(const char* path);

#ifdef __cplusplus
}
#endif
