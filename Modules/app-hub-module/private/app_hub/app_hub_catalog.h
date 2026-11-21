// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>


/** What a catalog list page has for an app */
struct AppHubCatalogEntry {
    std::string id;
    int32_t version_code = 0;
    std::string name;
    std::string short_description;
    /** False when the list says that the app has no icon. Without a statement, an app is assumed to have one. */
    bool has_icon = true;
    /** The index of the repository that the list came from */
    int32_t repository = 0;
};

/** What the details file of an app has on top of its catalog entry */
struct AppHubCatalogDetails {
    std::string version_name;
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
 * The catalog isn't kept in memory. The pages of the downloaded list are converted into a catalog file with one line per app,
 * which is read one entry at a time.
 */

/** @return true to add the entry to the catalog */
using AppHubCatalogAccept = bool (*)(const AppHubCatalogEntry& entry, void* context);

/**
 * Convert a page of the list of this device's platform and append its apps to a catalog file
 * @param[in] json_path the list page
 * @param[in] catalog_path the catalog file to append to, which is created when it doesn't exist
 * @param[out] page_count the number of pages of the list
 * @param[in] repository the index of the repository that the list is from, which is stored with each entry
 * @param[in] accept decides per entry whether it is added, or NULL to add all
 * @return false when the JSON is invalid or the catalog file can't be written
 */
bool app_hub_catalog_append_page(const char* json_path, const char* catalog_path, int32_t& page_count, int32_t repository = 0, AppHubCatalogAccept accept = nullptr, void* context = nullptr);

/**
 * Read the details file of an app
 * @return false when the JSON is invalid
 */
bool app_hub_catalog_read_details(const char* json_path, AppHubCatalogDetails& details);

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
bool app_hub_catalog_is_compatible(const AppHubCatalogDetails& details);

/** @return the PEM text of the certificate that verifies the official repository's server, or an empty string when it isn't configured */
const char* app_hub_catalog_get_certificate();

/** @return the base URL of the official repository */
const char* app_hub_catalog_get_official_url();

/**
 * @param[in] base_url the base URL of a repository, without a trailing slash
 * @return the URL of a page of the catalog list with the apps for this device's platform, numbered from 1
 */
std::string app_hub_catalog_get_page_url(const std::string& base_url, int32_t page);

/** @return the URL of a file that the catalog links to, e.g. a list page, details file, icon or package */
std::string app_hub_catalog_get_download_url(const std::string& base_url, const std::string& file);

/** @return the name of the details file of an app */
std::string app_hub_catalog_get_details_file(const std::string& id);

/** @return the name of the icon file of a version of an app */
std::string app_hub_catalog_get_icon_file(const std::string& id, int32_t version_code, bool mono);
