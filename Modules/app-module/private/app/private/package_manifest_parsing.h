// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/package_manifest.h>

#include <tactility/Allocator.hpp>

#include <map>
#include <string>
#include <vector>

/** Shared helpers + parser for app_package_manifest_parse() (source/package_manifest_parsing.cpp). */

bool app_package_manifest_get_value(const std::map<std::string, std::string>& properties, const std::string& key, std::string& out_value);

bool app_package_manifest_is_valid_format_version(const std::string& version);
bool app_package_manifest_is_valid_version_name(const std::string& version);
bool app_package_manifest_is_valid_version_code(const std::string& version);
bool app_package_manifest_is_valid_bool(const std::string& value);

/** Validates a comma-separated list of device ids (alphanumeric + '-' items, matching Devices/<id> folder names). */
bool app_package_manifest_is_valid_device_id_list(const std::string& value);

/** Validates a binary filename stem (AppManifestBinding::binary): alphanumeric plus '.', '_', '-'. */
bool app_package_manifest_is_valid_binary_name(const std::string& value);

/** Copies @a value into @a dest (a fixed-size buffer of @a dest_size bytes, including the NULL
 * terminator) if it fits.
 * @retval false @a value doesn't fit in @a dest_size bytes - @a dest is left untouched */
bool app_package_manifest_copy_bounded(char* dest, size_t dest_size, const std::string& value);

/** Parses a V2 (flat dot-notation, e.g. "app.version.name=...") manifest map into @a out_package
 * and (unless @a bindings_capacity is 0) its single AppManifestBinding, out_bindings[0].
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT a required key is missing or a value doesn't fit/validate
 * @retval ERROR_BUFFER_OVERFLOW @a bindings_capacity is nonzero but smaller than needed */
error_t package_manifest_parse_v2(const std::map<std::string, std::string>& properties, struct PackageManifest& out_package, struct AppManifestBinding* out_bindings, size_t bindings_capacity);

/** Parses a V3 (flat dot-notation with 0-indexed "app.N.*" blocks) manifest map into
 * @a out_package and up to @a bindings_capacity AppManifestBinding entries.
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT a required key is missing or a value doesn't fit/validate
 * @retval ERROR_BUFFER_OVERFLOW @a bindings_capacity is nonzero but smaller than the
 * manifest's app_manifest_count */
error_t package_manifest_parse_v3(const std::map<std::string, std::string>& properties, struct PackageManifest& out_package, struct AppManifestBinding* out_bindings, size_t bindings_capacity);

/** Collects the binary stems of the V3 "module.N.binary" entries (0-indexed, contiguous) into @a out_binaries.
 * @retval ERROR_NONE on success
 * @retval ERROR_INVALID_ARGUMENT a binary name is invalid, or the entries have a gap
 * @retval ERROR_BUFFER_OVERFLOW more than PACKAGE_MANIFEST_MAX_APP_MANIFESTS modules are declared */
error_t package_manifest_parse_module_binaries_v3(const std::map<std::string, std::string>& properties, std::vector<std::string>& out_binaries);

bool app_package_manifest_validate_string(const std::string& value, bool (*is_valid_char)(char));

/** Convenience wrapper around app_package_manifest_parse(): parses @a path once to learn
 * PackageManifest::app_manifest_count, then resizes @a out_bindings to fit exactly and parses
 * again to fill it - so the caller never needs to pre-allocate a fixed maximum.
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND the file doesn't exist / couldn't be opened
 * @retval ERROR_INVALID_ARGUMENT the file isn't a valid manifest, or a field's value doesn't fit
 * its fixed-size buffer */
error_t app_package_manifest_parse_into(const char* path, struct PackageManifest& out_package, std::vector<struct AppManifestBinding, tt::OptExternalAllocator<struct AppManifestBinding>>& out_bindings);

/** Reads the "module.N.binary" entries of the manifest at @a path into @a out_binaries. Manifest
 * versions without modules yield an empty list.
 * @retval ERROR_NONE on success
 * @retval ERROR_NOT_FOUND the file doesn't exist / couldn't be opened
 * @retval ERROR_INVALID_ARGUMENT the file isn't a valid manifest, or a module entry is invalid */
error_t app_package_manifest_parse_module_binaries(const char* path, std::vector<std::string>& out_binaries);
