// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/metadata.h>

#include <map>
#include <string>

/** Shared helpers + per-format parsers for app_metadata_parse() (source/app_metadata_parsing.cpp)
 * - split out like the old tt::app manifest parser (AppManifestParsing/V1/V2.cpp) that this is
 * modelled on, one file per format plus a shared dispatcher. */

bool app_metadata_get_value(const std::map<std::string, std::string>& properties, const std::string& key, std::string& out_value);

bool app_metadata_is_valid_format_version(const std::string& version);
bool app_metadata_is_valid_id(const std::string& id);
bool app_metadata_is_valid_name(const std::string& name);
bool app_metadata_is_valid_version_name(const std::string& version);
bool app_metadata_is_valid_version_code(const std::string& version);

/** Copies @a value into @a dest (a fixed-size buffer of @a dest_size bytes, including the NULL
 * terminator) if it fits.
 * @retval false @a value doesn't fit in @a dest_size bytes - @a dest is left untouched */
bool app_metadata_copy_bounded(char* dest, size_t dest_size, const std::string& value);

/** Parses a V1 (sectioned INI, e.g. "[app]versionName=...") manifest map into @a out_metadata. */
bool app_metadata_parse_v1(const std::map<std::string, std::string>& properties, struct AppMetadata& out_metadata);

/** Parses a V2 (flat dot-notation, e.g. "app.version.name=...") manifest map into @a out_metadata. */
bool app_metadata_parse_v2(const std::map<std::string, std::string>& properties, struct AppMetadata& out_metadata);
