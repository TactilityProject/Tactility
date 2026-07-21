// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the root path for user data. Survives OS upgrades.
 * @param[out] out_path buffer to store the path (no trailing "/")
 * @param[in] out_path_size size of the output buffer
 * @retval ERROR_NOT_FOUND if the configured storage location isn't available (e.g. no SD card)
 * @retval ERROR_BUFFER_OVERFLOW if out_path_size is too small
 * @retval ERROR_NONE on success
 */
error_t paths_get_user_data_path(char* out_path, size_t out_path_size);

#ifdef __cplusplus
}
#endif
