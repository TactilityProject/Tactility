// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/** File operations. All paths are absolute and already resolved, see fs.h. */

/** True if @a path exists and is a regular file. */
bool file_exists(const char* path);

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

/**
 * @brief Deletes a file.
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND @a path doesn't exist
 * @retval ERROR_RESOURCE an I/O error occurred
 */
error_t file_remove(const char* path);

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

#ifdef __cplusplus
}
#endif
