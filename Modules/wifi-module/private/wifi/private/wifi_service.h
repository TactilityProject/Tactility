// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The service id, which also determines where the settings are stored */
#define WIFI_SERVICE_ID "tactility.wifi"

extern const struct ServiceManifest wifi_service_manifest;

/** The "wifi" command-line app */
extern const struct AppManifest wifi_command_manifest;

/** Imports the "*.ap.properties" files from the "provisioning" directory in the data path. */
void wifi_provisioning_import(void);

/**
 * Create a directory and its missing parents.
 * @retval ERROR_RESOURCE a directory could not be created
 * @retval ERROR_NONE on success
 */
error_t wifi_ensure_directory_exists(const char* path);

#ifdef __cplusplus
}
#endif
