// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/manifest.h>

#include <tactility/error.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PACKAGE_MANIFEST_TARGET_SDK_LENGTH 16
#define PACKAGE_MANIFEST_ID_LENGTH 32
#define PACKAGE_MANIFEST_VERSION_NAME_LENGTH 16
#define PACKAGE_MANIFEST_REQUIRES_DEVICE_ID_LENGTH 64

/** Fixed-capacity buffer for a PackageManifest::id value - always NUL-terminated. */
typedef char PackageId[PACKAGE_MANIFEST_ID_LENGTH + 1];

/** Character count, excluding null terminator, for AppManifestBinding::binary. */
#define APP_MANIFEST_BINARY_LENGTH 31

/** Largest number of AppManifest entries package_manifest_parse() can produce from a single
 * manifest.properties file. */
#define PACKAGE_MANIFEST_MAX_APP_MANIFESTS 32

/** A package-level manifest.properties: SDK/version metadata that applies to the whole package,
 * not to any one of its (possibly several) apps. Not kept in memory at runtime - only used to
 * create and cache the AppManifest(s) it describes. */
struct PackageManifest {
    /**
     * The package identifier (e.g. the install directory name). Distinct from any of its own
     * AppManifest ids.
     * Must be NULL-terminated.
     */
    PackageId id;

    /**
     * The package version as it is displayed to the user (e.g. "1.2.0")
     * Must be NULL-terminated.
     */
    char version_name[PACKAGE_MANIFEST_VERSION_NAME_LENGTH + 1];

    /** The package's technical version (must be incremented with new releases). */
    uint64_t version_code;

    /**
     * The SDK version that was used to compile this package. (e.g. "0.6.0")
     * Must be NULL-terminated.
     */
    char target_sdk[PACKAGE_MANIFEST_TARGET_SDK_LENGTH + 1];

    /**
     * Comma-separated list of device ids the package is restricted to (e.g. "m5stack-tab5"),
     * matching the folder names under Devices/. Empty means unrestricted.
     * Must be NULL-terminated.
     */
    char requires_device_id[PACKAGE_MANIFEST_REQUIRES_DEVICE_ID_LENGTH + 1];

    /** How many AppManifest entries this package's manifest.properties declared. */
    uint32_t app_manifest_count;
};

/** Pairs a parsed AppManifest with the filename (without extension) it installs as under
 * bin/<platform>/ - e.g. "main" resolves to bin/posix-x86_64/main.so. Not kept anywhere at
 * runtime - same lifetime as PackageManifest, only used to hand parse results to the installer/
 * scanner, which resolve `binary` into AppManifest::location::location. */
struct AppManifestBinding {
    struct AppManifest manifest;
    char binary[APP_MANIFEST_BINARY_LENGTH + 1];
};

/**
 * Parses a manifest.properties file at @a path (flat dot-notation, e.g. "app.id=...") into
 * @a out_package and @a out_bindings.
 * @param[out] out_bindings written with up to @a bindings_capacity entries (see
 * PackageManifest::app_manifest_count for how many)
 * @param[in] bindings_capacity the capacity of @a out_bindings
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND the file doesn't exist / couldn't be opened
 * @retval ERROR_INVALID_ARGUMENT the file isn't a valid manifest, or a field's value doesn't fit
 * its fixed-size buffer
 * @retval ERROR_BUFFER_OVERFLOW the manifest declares more apps than @a bindings_capacity
 */
error_t app_package_manifest_parse(const char* path, struct PackageManifest* out_package, struct AppManifestBinding* out_bindings, size_t bindings_capacity);

#ifdef __cplusplus
}
#endif
