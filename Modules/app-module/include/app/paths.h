// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the user data directory for an app. Survives OS upgrades. No trailing "/".
 * @param[in] app_id non-null app id
 * @param[out] out_path buffer to store the path
 * @param[in] out_path_size size of the output buffer
 * @retval ERROR_BUFFER_OVERFLOW if out_path_size is too small
 * @retval ERROR_NONE on success
 */
error_t app_paths_get_user_data_directory(const char* app_id, char* out_path, size_t out_path_size);

/**
 * @brief Get a path within the user data directory for an app.
 * @param[in] app_id non-null app id
 * @param[in] child_path path without a "/" prefix
 * @param[out] out_path buffer to store the path
 * @param[in] out_path_size size of the output buffer
 * @retval ERROR_BUFFER_OVERFLOW if out_path_size is too small
 * @retval ERROR_NONE on success
 */
error_t app_paths_get_user_data_path(const char* app_id, const char* child_path, char* out_path, size_t out_path_size);

/**
 * @brief Get the assets directory for an app. Do not store configuration data here. No trailing "/".
 * @param[in] app_id non-null app id
 * @param[out] out_path buffer to store the path
 * @param[in] out_path_size size of the output buffer
 * @retval ERROR_BUFFER_OVERFLOW if out_path_size is too small
 * @retval ERROR_NONE on success
 */
error_t app_paths_get_assets_directory(const char* app_id, char* out_path, size_t out_path_size);

/**
 * @brief Get a path within the assets directory for an app.
 * @param[in] app_id non-null app id
 * @param[in] child_path path without a "/" prefix
 * @param[out] out_path buffer to store the path
 * @param[in] out_path_size size of the output buffer
 * @retval ERROR_BUFFER_OVERFLOW if out_path_size is too small
 * @retval ERROR_NONE on success
 */
error_t app_paths_get_assets_path(const char* app_id, const char* child_path, char* out_path, size_t out_path_size);

#ifdef __cplusplus
}
#endif
