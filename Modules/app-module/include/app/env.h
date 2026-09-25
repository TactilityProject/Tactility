// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>

#include <stdbool.h>

/**
 * Per-app-instance environment variables, as "NAME=VALUE" strings scoped to the calling app instance.
 * Seeded from AppStartContext's own `environment` (app/start.h) at start, and mutable at runtime.
 * Every function here acts on app_scheduler_current_app_id()'s own instance; there is no way to read
 * or change another instance's environment.
 *
 * Matches POSIX semantics.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sets @a name to @a value in the calling app instance's environment, adding it if not already present.
 * @param[in] name must be non-empty and not contain '='
 * @param[in] overwrite if false and @a name is already set, the existing value is left untouched
 * (not an error)
 * @retval ERROR_INVALID_ARGUMENT @a name or @a value is NULL, @a name is empty, or @a name
 * contains '='
 * @retval ERROR_NOT_FOUND the calling task isn't a running app instance
 * @retval ERROR_NONE on success
 */
error_t app_env_set(const char* name, const char* value, bool overwrite);

/**
 * Removes @a name from the calling app instance's environment, if present. Not an error if it wasn't set.
 * @param[in] name must be non-empty and not contain '='
 * @retval ERROR_INVALID_ARGUMENT @a name is NULL, empty, or contains '='
 * @retval ERROR_NOT_FOUND the calling task isn't a running app instance
 * @retval ERROR_NONE on success
 */
error_t app_env_unset(const char* name);

/**
 * @param[in] name must be non-empty and not contain '='
 * @return the value of @a name in the calling app instance's environment, or NULL if unset, @a  name is invalid,
 * or the calling task isn't a running app instance.
 * @warning As with POSIX getenv(), the returned pointer is only valid until the next app_env_*
 * call on this instance (from any task); copy it if it needs to outlive that.
 */
const char* app_env_get(const char* name);

/**
 * Sets an environment variable for the calling app instance from a single "NAME=VALUE" string.
 * @param[in] string must contain '=' with a non-empty name before it
 * @retval ERROR_INVALID_ARGUMENT @a string is NULL, has no '=', or the name before it is empty
 * @retval ERROR_NOT_FOUND the calling task isn't a running app instance
 * @retval ERROR_NONE on success
 */
error_t app_env_put(const char* string);

#ifdef __cplusplus
}
#endif
