// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_METADATA_TARGET_SDK_LENGTH 16
#define APP_METADATA_APP_ID_LENGTH 32
#define APP_METADATA_APP_NAME_LENGTH 32
#define APP_METADATA_APP_VERSION_NAME_LENGTH 16

struct AppMetadata {

    /**
     * The SDK version that was used to compile this app. (e.g. "0.6.0")
     * Must be NULL-terminated.
     */
    char target_sdk[APP_METADATA_TARGET_SDK_LENGTH + 1];

    /**
     * The identifier by which the app is launched by the system and other apps.
     * Must be NULL-terminated.
     */
    char app_id[APP_METADATA_APP_ID_LENGTH + 1];

    /**
     * The user-readable name of the app. Used in UI.
     * Must be NULL-terminated.
     */
    char app_name[APP_METADATA_APP_NAME_LENGTH + 1];

    /**
     * The version as it is displayed to the user (e.g. "1.2.0")
     * Must be NULL-terminated.
     */
    char app_version_name[APP_METADATA_APP_VERSION_NAME_LENGTH + 1];

    /** The technical version (must be incremented with new releases of the app) */
    uint64_t app_version_code;
};

/**
 * Parses a manifest.properties file at @a path into @a out_metadata, auto-detecting the V1
 * (sectioned, e.g. "[app]id=...") or V2 (flat dot-notation, e.g. "app.id=...") format from its
 * first line.
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND the file doesn't exist / couldn't be opened
 * @retval ERROR_INVALID_ARGUMENT the file isn't a valid manifest, or a field's value doesn't fit
 * @a out_metadata's fixed-size buffers
 */
error_t app_metadata_parse(const char* path, struct AppMetadata* out_metadata);

#ifdef __cplusplus
}
#endif
