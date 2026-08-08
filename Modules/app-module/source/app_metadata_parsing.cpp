// SPDX-License-Identifier: Apache-2.0
#include "tactility/filesystem/file_mutex.h"


#include <app/metadata.h>

#include <app/private/app_metadata_parsing_internal.h>

#include <tactility/log.h>

#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

constexpr auto* TAG = "app_metadata";

namespace {

std::string trim(const std::string& value) {
    constexpr auto* whitespace = " \t\r\n";
    auto start = value.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }
    auto end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

bool validate_string(const std::string& value, bool (*is_valid_char)(char)) {
    for (char c: value) {
        if (!is_valid_char(c)) {
            return false;
        }
    }
    return true;
}

/** manifest.properties format: "key=value" lines, "[section]" lines prefix every following key
 * until the next section, "#" lines are comments, blank lines are skipped. Deliberately a local,
 * minimal re-implementation rather than depending on Tactility's file::loadPropertiesFile() -
 * app-module (like every other kernel module) may not depend upward on the Tactility layer. */
bool load_properties(const std::string& path, std::map<std::string, std::string>& out_properties, std::string& out_first_line) {
    FileMutex mutex;
    file_mutex_get(&mutex, path.c_str());
    file_mutex_lock(&mutex);

    std::ifstream file(path);
    if (!file.is_open()) {
        file_mutex_unlock(&mutex);
        return false;
    }

    std::string line;
    std::string section_prefix;
    bool got_first_line = false;
    while (std::getline(file, line)) {
        auto trimmed_line = trim(line);

        if (trimmed_line.empty() || trimmed_line.starts_with("#")) {
            continue;
        }

        if (!got_first_line) {
            out_first_line = trimmed_line;
            got_first_line = true;
        }

        if (trimmed_line.starts_with("[")) {
            section_prefix = trimmed_line;
            continue;
        }

        auto separator_index = trimmed_line.find('=');
        if (separator_index == std::string::npos) {
            LOG_E(TAG, "Failed to parse manifest line (skipped): %s", trimmed_line.c_str());
            continue;
        }

        auto key = section_prefix + trim(trimmed_line.substr(0, separator_index));
        auto value = trim(trimmed_line.substr(separator_index + 1));
        out_properties[key] = value;
    }

    file_mutex_unlock(&mutex);
    return true;
}

} // namespace

bool app_metadata_get_value(const std::map<std::string, std::string>& properties, const std::string& key, std::string& out_value) {
    const auto iterator = properties.find(key);
    if (iterator == properties.end()) {
        LOG_E(TAG, "Failed to find %s in manifest", key.c_str());
        return false;
    }
    out_value = iterator->second;
    return true;
}

bool app_metadata_is_valid_format_version(const std::string& version) {
    return !version.empty() && validate_string(version, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.';
    });
}

bool app_metadata_is_valid_id(const std::string& id) {
    return id.size() >= 5 && id.size() <= APP_METADATA_APP_ID_LENGTH && validate_string(id, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.';
    });
}

bool app_metadata_is_valid_name(const std::string& name) {
    return name.size() >= 2 && name.size() <= APP_METADATA_APP_NAME_LENGTH && validate_string(name, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == ' ' || c == '-';
    });
}

bool app_metadata_is_valid_version_name(const std::string& version) {
    return !version.empty() && version.size() <= APP_METADATA_APP_VERSION_NAME_LENGTH && validate_string(version, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '-' || c == '_';
    });
}

bool app_metadata_is_valid_version_code(const std::string& version) {
    // 20 digits is the maximum decimal width of uint64_t.
    return !version.empty() && version.size() <= 20 && validate_string(version, [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
}

bool app_metadata_copy_bounded(char* dest, size_t dest_size, const std::string& value) {
    if (value.size() >= dest_size) {
        return false;
    }
    memcpy(dest, value.c_str(), value.size() + 1);
    return true;
}

error_t app_metadata_parse(const char* path, struct AppMetadata* out_metadata) {
    LOG_I(TAG, "Parsing manifest %s", path);

    std::map<std::string, std::string> properties;
    std::string first_line;
    if (!load_properties(path, properties, first_line)) {
        LOG_E(TAG, "Failed to load manifest at %s", path);
        return ERROR_NOT_FOUND;
    }

    // The V1 format's first line is always the literal "[manifest]" section header; V2 files are
    // flat from the first line onward.
    bool is_v1_format = first_line == "[manifest]";
    bool success = is_v1_format
        ? app_metadata_parse_v1(properties, *out_metadata)
        : app_metadata_parse_v2(properties, *out_metadata);

    return success ? ERROR_NONE : ERROR_INVALID_ARGUMENT;
}
