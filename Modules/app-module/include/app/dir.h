// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stddef.h>

#include <tactility/error.h>

/**
 * Per-app-instance current working directory, scoped to the calling app instance. Inherited from
 * the parent instance at start (like app/env.h's environment), defaulting to "/" for a top-level
 * instance, and mutable at runtime via app_dir_set_cwd(). Every function here acts on
 * app_scheduler_current_app_id()'s own instance; there is no way to read or change another
 * instance's cwd.
 *
 * Matches POSIX getcwd()/chdir() semantics, and is what their real-syscall wraps
 * (Modules/app-module/source/stdio_wrap.cpp) route to for a calling task that is an app instance.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @param[in,out] size capacity of @a buf on input; unchanged on output
 * @retval ERROR_NOT_FOUND the calling task isn't a running app instance
 * @retval ERROR_BUFFER_OVERFLOW @a buf is too small
 * @retval ERROR_NONE on success
 */
error_t app_dir_get_cwd(char* buf, size_t size);

/**
 * @param[in] absolute_path must be "/" or an existing directory; not resolved or normalized -
 * the caller resolves relative segments against app_dir_get_cwd() itself first.
 * @retval ERROR_NOT_FOUND the calling task isn't a running app instance, or @a absolute_path
 * doesn't exist
 * @retval ERROR_INVALID_ARGUMENT @a absolute_path is NULL or not absolute
 * @retval ERROR_NONE on success
 */
error_t app_dir_set_cwd(const char* absolute_path);

#ifdef __cplusplus
}
#endif
