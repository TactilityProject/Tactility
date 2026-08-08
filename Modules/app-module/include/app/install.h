// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Computes the install directory for @a app_id (does not check whether anything is actually
 * installed there).
 * @param[out] path always NULL-terminated on return, even on failure (empty string if
 * @a path_size == 0 - nothing is written in that case; otherwise at least "" is written)
 * @retval ERROR_NONE on success
 * @retval ERROR_BUFFER_OVERFLOW @a path_size is too small to hold the path (including the
 * NULL terminator)
 * @retval ERROR_NOT_FOUND the app install location isn't available (e.g. no SD card)
 */
error_t app_get_install_path(const char* app_id, char* path, size_t path_size);

/**
 * Installs an app from a tarball at @a source_path: extracts it into the app install directory,
 * parses the extracted manifest.properties (see app/metadata.h) to determine its id, then
 * registers it with app_manager_add() as an AppLocation{APP_LOCATION_PATH, <install dir>} app.
 * If an app with the same id is already installed (via a previous app_install() call), it is
 * uninstalled first - stopped if running, its old install directory removed - before the new
 * one takes its place.
 * @param[in] source_path path to a tar file containing the app (must have manifest.properties
 * at its root)
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND @a source_path doesn't exist / can't be read
 * @retval ERROR_INVALID_ARGUMENT the tarball has no valid manifest.properties at its root
 */
error_t app_install(const char* source_path);

/**
 * Uninstalls a previously app_install()-ed app: stops it if currently running, deletes its
 * install directory, and unregisters it (app_manager_remove()).
 * @param[in] app_id the id the app was installed under (AppMetadata::app_id)
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND no such app was installed via app_install()
 */
error_t app_uninstall(const char* app_id);

#ifdef __cplusplus
}
#endif
