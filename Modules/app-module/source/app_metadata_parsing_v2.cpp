// SPDX-License-Identifier: Apache-2.0
#include <app/metadata.h>
#include <app/private/app_metadata_parsing_internal.h>

#include <charconv>

#include <tactility/log.h>

constexpr auto* TAG = "app_metadata_v2";

bool app_metadata_parse_v2(const std::map<std::string, std::string>& properties, AppMetadata& out_metadata) {
    // manifest

    std::string format_version;
    if (!app_metadata_get_value(properties, "manifest.version", format_version)) {
        return false;
    }

    if (!app_metadata_is_valid_format_version(format_version)) {
        LOG_E(TAG, "Invalid version");
        return false;
    }

    // app

    std::string id;
    if (!app_metadata_get_value(properties, "app.id", id)) {
        return false;
    }

    if (!app_metadata_is_valid_id(id)) {
        LOG_E(TAG, "Invalid app id");
        return false;
    }

    if (!app_metadata_copy_bounded(out_metadata.app_id, sizeof(out_metadata.app_id), id)) {
        LOG_E(TAG, "App id too long");
        return false;
    }

    std::string name;
    if (!app_metadata_get_value(properties, "app.name", name)) {
        return false;
    }

    if (!app_metadata_is_valid_name(name)) {
        LOG_E(TAG, "Invalid app name");
        return false;
    }

    if (!app_metadata_copy_bounded(out_metadata.app_name, sizeof(out_metadata.app_name), name)) {
        LOG_E(TAG, "App name too long");
        return false;
    }

    std::string version_name;
    if (!app_metadata_get_value(properties, "app.version.name", version_name)) {
        return false;
    }

    if (!app_metadata_is_valid_version_name(version_name)) {
        LOG_E(TAG, "Invalid app version name");
        return false;
    }

    if (!app_metadata_copy_bounded(out_metadata.app_version_name, sizeof(out_metadata.app_version_name), version_name)) {
        LOG_E(TAG, "App version name too long");
        return false;
    }

    std::string version_code_string;
    if (!app_metadata_get_value(properties, "app.version.code", version_code_string)) {
        return false;
    }

    if (!app_metadata_is_valid_version_code(version_code_string)) {
        LOG_E(TAG, "Invalid app version code");
        return false;
    }

    uint64_t version_code = 0;
    const auto* first = version_code_string.data();
    const auto* last = first + version_code_string.size();
    if (std::from_chars(first, last, version_code).ec != std::errc {}) {
        LOG_E(TAG, "App version code out of range");
        return false;
    }
    out_metadata.app_version_code = version_code; // [target]

    // target

    std::string target_sdk;
    if (!app_metadata_get_value(properties, "target.sdk", target_sdk)) {
        return false;
    }

    if (!app_metadata_copy_bounded(out_metadata.target_sdk, sizeof(out_metadata.target_sdk), target_sdk)) {
        LOG_E(TAG, "Target sdk too long");
        return false;
    }

    return true;
}
