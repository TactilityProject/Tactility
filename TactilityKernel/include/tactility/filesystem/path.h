// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Operations on a path that can be a file or a directory. All paths are absolute and already resolved, see fs.h. */

/** True if @a path exists, file or directory. */
bool path_exists(const char* path);

/**
 * @brief Total size in bytes of a file, or of everything under a directory.
 * @return the size, or 0 if @a path doesn't exist
 */
uint64_t path_tree_size(const char* path);

#ifdef __cplusplus
}
#endif
