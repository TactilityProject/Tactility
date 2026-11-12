// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The service id, which also determines where the settings are stored */
#define BLUETOOTH_SERVICE_ID "tactility.bluetooth"

extern const struct ServiceManifest bluetooth_service_manifest;

/**
 * Create a directory and its missing parents.
 * @retval ERROR_RESOURCE a directory could not be created
 * @retval ERROR_NONE on success
 */
error_t bluetooth_ensure_directory_exists(const char* path);

#ifdef __cplusplus
}
#endif
