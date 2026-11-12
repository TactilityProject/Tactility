// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>


struct AppHubCatalogEntry {
    std::string id;
    std::string version_name;
    int32_t version_code = 0;
    std::string name;
    std::string description;
    std::string target_sdk;
    std::vector<std::string> target_platforms;
    /** Device ids the app is restricted to. Empty means unrestricted. */
    std::vector<std::string> requires_device_id;
    /** RAM in megabytes the app needs. 0 means no requirement. */
    int32_t requires_ram = 0;
    /** Path of the package, relative to the catalog's base URL */
    std::string file;
};

/**
 * The catalog isn't kept in memory. A downloaded apps.json is converted once into a catalog file with one line per app,
 * which is read one entry at a time.
 */

/**
 * Convert an apps.json file into a catalog file
 * @param[in] json_path the apps.json file
 * @param[in] catalog_path the catalog file to write, which is removed when the conversion fails
 * @return false when the JSON is invalid or the catalog file can't be written
 */
bool app_hub_catalog_convert(const char* json_path, const char* catalog_path);

/** @return true to continue, false to stop */
using AppHubCatalogVisitor = bool (*)(const AppHubCatalogEntry& entry, void* context);

/**
 * Visit the entries of a catalog file in order
 * @return false when the file can't be read or is malformed
 */
bool app_hub_catalog_for_each(const char* catalog_path, AppHubCatalogVisitor visitor, void* context);

/**
 * @param[out] entry the entry with the id
 * @return false when the entry isn't in the catalog file
 */
bool app_hub_catalog_find(const char* catalog_path, const char* id, AppHubCatalogEntry& entry);

/** @return the platform name of this device in the catalog's target platforms, e.g. "esp32s3" or "linux-x86_64" */
const char* app_hub_catalog_get_platform();

/** @return true when this device can install and run the entry's app (target platform, device and RAM requirements) */
bool app_hub_catalog_is_compatible(const AppHubCatalogEntry& entry);

/** @return the PEM text of the certificate that verifies the catalog server, or an empty string when it isn't configured */
const char* app_hub_catalog_get_certificate();

std::string app_hub_catalog_get_url();

std::string app_hub_catalog_get_download_url(const std::string& file);
