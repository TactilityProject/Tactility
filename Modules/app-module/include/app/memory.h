// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Counts an allocation made by the calling app instance's own code. When the app ends with
 * allocations left, a warning is logged. Does nothing outside an app's own task.
 * @param[in] size the allocated block's size
 */
void app_memory_record_alloc(size_t size);

/**
 * Counts a free by the calling app instance's own code. Does nothing outside an app's own task.
 * @param[in] size the freed block's size
 */
void app_memory_record_free(size_t size);

#ifdef __cplusplus
}
#endif
