// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>

#include <stddef.h>

struct Module;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Computes the install directory for @a app_id (does not check whether anything is actually
 * installed there).
 * @param[out] path always NULL-terminated on return, even on failure. Empty when @a path_size
 * is 0, since nothing is written in that case.
 * @retval ERROR_NONE on success
 * @retval ERROR_BUFFER_OVERFLOW @a path_size is too small to hold the path (including the
 * NULL terminator)
 * @retval ERROR_NOT_FOUND the app install location isn't available (e.g. no SD card)
 */
error_t app_get_install_path(const char* app_id, char* path, size_t path_size);

/**
 * Installs a package from a tarball at @a source_path: extracts it into the package's install
 * directory, parses the extracted manifest.properties (see app/package_manifest.h) into a
 * PackageManifest and one or more AppManifestBindings, then registers each with app_manager_add()
 * as an AppLocation{APP_LOCATION_PATH, <binary path>} app.
 * Binaries listed as "module.N.binary" in the manifest are kernel modules instead: their main() takes
 * no arguments and returns the `struct Module*` to start, and they stay loaded until the package is uninstalled.
 * If a module fails to load, the whole install is undone.
 * If a package with the same id is already installed (via a previous app_install() call), it is
 * uninstalled first: every one of its apps stopped if running, then its old install directory
 * removed, before the new one takes its place.
 * @param[in] source_path path to a tar file containing the package (must have
 * manifest.properties at its root)
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND @a source_path doesn't exist / can't be read
 * @retval ERROR_INVALID_ARGUMENT the tarball has no valid manifest.properties at its root
 * @retval ERROR_RESOURCE_BUSY the package replaces an installed one that has a kernel module in use
 */
error_t app_install(const char* source_path);

/** @return true while an app_install() call is running on any task */
bool app_install_is_active(void);

/**
 * Uninstalls a previously app_install()-ed package: stops every one of its apps if currently
 * running, deletes its install directory, and unregisters all of them (app_manager_remove()).
 * @param[in] app_id the package id it was installed under (PackageManifest::id)
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND no such package was installed via app_install()
 * @retval ERROR_RESOURCE_BUSY a kernel module of the package is in use and can't be stopped. Nothing was changed.
 */
error_t app_uninstall(const char* app_id);

/** A kernel module that was loaded from an installed package. */
struct AppModuleInfo {
    /** The id of the package that holds the module (see app_uninstall()). */
    const char* package_id;
    /** The package's install directory. */
    const char* install_dir;
    /** The running module. */
    struct Module* module;
};

/**
 * @warning Runs with the module loader's internal lock held. Do not call app_install(), app_uninstall()
 * or any other loader function from inside the visitor. Copy out what you need and act on it after
 * app_module_for_each() returns. The pointers in @a info are only valid during the call.
 */
typedef void (*AppModuleVisitorFn)(const struct AppModuleInfo* info, void* context);

/**
 * Calls @a visitor once for every kernel module that was loaded from an installed package
 * (see "module.N.binary" in app_install()). Modules built into the firmware are not visited.
 * Iteration order is unspecified.
 */
void app_module_for_each(AppModuleVisitorFn visitor, void* context);

/**
 * Starts or stops a kernel module that was loaded from an installed package, with the module loader locked.
 * An uninstall or install can't unload the module's binary meanwhile, which is why this exists instead of
 * keeping the `struct Module*` from app_module_for_each().
 * Starting a started module, or stopping a stopped one, does nothing.
 * @warning Don't call it from the visitor of app_module_for_each(), or from the module's own start() and stop().
 * @param[in] package_id the id of the package that holds the module (AppModuleInfo::package_id)
 * @param[in] module_name the name of the module (struct Module::name)
 * @param[in] start true to start the module, false to stop it
 * @retval ERROR_NONE the module is in the requested state
 * @retval ERROR_NOT_FOUND no such module is loaded
 * @retval ERROR_RESOURCE_BUSY the module is in use and can't be stopped (see module_stop())
 * @return otherwise the result of module_start() or module_stop()
 */
error_t app_module_set_started(const char* package_id, const char* module_name, bool start);

#ifdef __cplusplus
}
#endif
