// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the user data directory for a service. Survives OS upgrades. No trailing "/".
 * @param[in] service_id non-null service id
 * @param[out] out_path buffer to store the path
 * @param[in] out_path_size size of the output buffer
 * @retval ERROR_BUFFER_OVERFLOW if out_path_size is too small
 * @retval ERROR_NONE on success
 */
error_t service_paths_get_user_data_directory(const char* service_id, char* out_path, size_t out_path_size);

/**
 * @brief Get a path within the user data directory for a service.
 * @param[in] service_id non-null service id
 * @param[in] child_path path without a "/" prefix
 * @param[out] out_path buffer to store the path
 * @param[in] out_path_size size of the output buffer
 * @retval ERROR_BUFFER_OVERFLOW if out_path_size is too small
 * @retval ERROR_NONE on success
 */
error_t service_paths_get_user_data_path(const char* service_id, const char* child_path, char* out_path, size_t out_path_size);

/**
 * @brief Get the assets directory for a service. Do not store configuration data here. No trailing "/".
 * @param[in] service_id non-null service id
 * @param[out] out_path buffer to store the path
 * @param[in] out_path_size size of the output buffer
 * @retval ERROR_BUFFER_OVERFLOW if out_path_size is too small
 * @retval ERROR_NONE on success
 */
error_t service_paths_get_assets_directory(const char* service_id, char* out_path, size_t out_path_size);

/**
 * @brief Get a path within the assets directory for a service.
 * @param[in] service_id non-null service id
 * @param[in] child_path path without a "/" prefix
 * @param[out] out_path buffer to store the path
 * @param[in] out_path_size size of the output buffer
 * @retval ERROR_BUFFER_OVERFLOW if out_path_size is too small
 * @retval ERROR_NONE on success
 */
error_t service_paths_get_assets_path(const char* service_id, const char* child_path, char* out_path, size_t out_path_size);

#ifdef __cplusplus
}
#endif
