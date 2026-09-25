// SPDX-License-Identifier: Apache-2.0
#include "app/manifest.h"


#include <app/package_manifest.h>
#include <app/private/package_manifest_parsing.h>

#include <charconv>
#include <format>
#include <optional>

#include <tactility/log.h>

constexpr auto* TAG = "app_metadata_v3";

namespace {

// Numeric index out of an "app.<index>.<rest>" key, or nullopt if it doesn't match that shape.
std::optional<size_t> parse_app_key_index(const std::string& key) {
    constexpr auto* prefix = "app.";
    constexpr size_t prefix_len = 4;
    if (!key.starts_with(prefix)) {
        return std::nullopt;
    }
    auto dot = key.find('.', prefix_len);
    if (dot == std::string::npos || dot == prefix_len) {
        return std::nullopt;
    }
    size_t index = 0;
    const auto* first = key.data() + prefix_len;
    const auto* last = key.data() + dot;
    auto result = std::from_chars(first, last, index);
    if (result.ec != std::errc {} || result.ptr != last) {
        return std::nullopt;
    }
    return index;
}

// Parses one "app.<index>.*" block into @a out_binding.
error_t parse_app_manifest(const std::map<std::string, std::string>& properties, size_t index, AppManifestBinding& out_binding) {
    auto prefix = std::format("app.{}.", index);
    AppManifest& out_manifest = out_binding.manifest;

    std::string id;
    if (!app_package_manifest_get_value(properties, prefix + "id", id)) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (!app_manifest_id_is_valid(id.c_str())) {
        LOG_E(TAG, "Invalid %sid", prefix.c_str());
        return ERROR_INVALID_ARGUMENT;
    }
    if (!app_package_manifest_copy_bounded(out_manifest.id, sizeof(out_manifest.id), id)) {
        LOG_E(TAG, "%sid too long", prefix.c_str());
        return ERROR_INVALID_ARGUMENT;
    }

    std::string binary;
    if (!app_package_manifest_get_value(properties, prefix + "binary", binary)) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (!app_package_manifest_is_valid_binary_name(binary)) {
        LOG_E(TAG, "Invalid %sbinary", prefix.c_str());
        return ERROR_INVALID_ARGUMENT;
    }
    // Filename (without extension) this app installs as under bin/<platform>/ - see
    // app_package_manifest_parse()'s own doc.
    if (!app_package_manifest_copy_bounded(out_binding.binary, sizeof(out_binding.binary), binary)) {
        LOG_E(TAG, "%sbinary too long", prefix.c_str());
        return ERROR_INVALID_ARGUMENT;
    }

    std::string name;
    if (!app_package_manifest_get_value(properties, prefix + "name", name)) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (!app_manifest_name_is_valid(name.c_str())) {
        LOG_E(TAG, "Invalid %sname", prefix.c_str());
        return ERROR_INVALID_ARGUMENT;
    }
    if (!app_package_manifest_copy_bounded(out_manifest.name, sizeof(out_manifest.name), name)) {
        LOG_E(TAG, "%sname too long", prefix.c_str());
        return ERROR_INVALID_ARGUMENT;
    }

    // <index>.stack.depth (optional; if present, must be a valid unsigned decimal fitting uint32_t)
    auto stack_size_iterator = properties.find(prefix + "stack.depth");
    if (stack_size_iterator != properties.end()) {
        const std::string& stack_size_string = stack_size_iterator->second;
        if (!app_manifest_stack_size_is_valid(stack_size_string.c_str())) {
            LOG_E(TAG, "Invalid %sstack.depth", prefix.c_str());
            return ERROR_INVALID_ARGUMENT;
        }

        uint32_t stack_size = 0;
        const auto* first = stack_size_string.data();
        const auto* last = first + stack_size_string.size();
        if (std::from_chars(first, last, stack_size).ec != std::errc {}) {
            LOG_E(TAG, "%sstack.depth out of range", prefix.c_str());
            return ERROR_INVALID_ARGUMENT;
        }
        // Reject outright rather than truncating/clamping into AppStackConfig::depth (uint16_t) -
        // a value like 1073741825 would otherwise silently narrow to 1, handing the app a
        // catastrophically undersized stack instead of the huge one it declared.
        if (stack_size > APP_STACK_SIZE_MAX) {
            LOG_E(TAG, "%sstack.depth %u exceeds APP_STACK_SIZE_MAX(%u)", prefix.c_str(), stack_size, APP_STACK_SIZE_MAX);
            return ERROR_INVALID_ARGUMENT;
        }
        out_manifest.stack.depth = static_cast<uint16_t>(stack_size);
    }

    out_manifest.flags = 0;

    // <index>.hidden (optional; defaults to false)
    auto hidden_iterator = properties.find(prefix + "hidden");
    if (hidden_iterator != properties.end()) {
        if (!app_package_manifest_is_valid_bool(hidden_iterator->second)) {
            LOG_E(TAG, "Invalid %shidden", prefix.c_str());
            return ERROR_INVALID_ARGUMENT;
        }
        if (hidden_iterator->second == "true") {
            out_manifest.flags |= APP_MANIFEST_FLAG_HIDDEN;
        }
    }

    // <index>.headless (optional; defaults to false)
    auto headless_iterator = properties.find(prefix + "headless");
    if (headless_iterator != properties.end()) {
        if (!app_package_manifest_is_valid_bool(headless_iterator->second)) {
            LOG_E(TAG, "Invalid %sheadless", prefix.c_str());
            return ERROR_INVALID_ARGUMENT;
        }
        if (headless_iterator->second == "true") {
            out_manifest.flags |= APP_MANIFEST_FLAG_HEADLESS;
        }
    }

    out_manifest.category = APP_CATEGORY_USER;

    return ERROR_NONE;
}

} // namespace

error_t package_manifest_parse_v3(const std::map<std::string, std::string>& properties, PackageManifest& out_package, AppManifestBinding* out_bindings, size_t bindings_capacity) {
    // package-level fields: bare keys, no "app." prefix (that's reserved for the app.<index>.*
    // blocks below).

    std::string id;
    if (!app_package_manifest_get_value(properties, "id", id)) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (!app_manifest_id_is_valid(id.c_str())) {
        LOG_E(TAG, "Invalid id");
        return ERROR_INVALID_ARGUMENT;
    }
    if (!app_package_manifest_copy_bounded(out_package.id, sizeof(out_package.id), id)) {
        LOG_E(TAG, "id too long");
        return ERROR_INVALID_ARGUMENT;
    }

    std::string version_name;
    if (!app_package_manifest_get_value(properties, "version.name", version_name)) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (!app_package_manifest_is_valid_version_name(version_name)) {
        LOG_E(TAG, "Invalid version.name");
        return ERROR_INVALID_ARGUMENT;
    }
    if (!app_package_manifest_copy_bounded(out_package.version_name, sizeof(out_package.version_name), version_name)) {
        LOG_E(TAG, "version.name too long");
        return ERROR_INVALID_ARGUMENT;
    }

    std::string version_code_string;
    if (!app_package_manifest_get_value(properties, "version.code", version_code_string)) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (!app_package_manifest_is_valid_version_code(version_code_string)) {
        LOG_E(TAG, "Invalid version.code");
        return ERROR_INVALID_ARGUMENT;
    }
    uint64_t version_code = 0;
    const auto* first = version_code_string.data();
    const auto* last = first + version_code_string.size();
    if (std::from_chars(first, last, version_code).ec != std::errc {}) {
        LOG_E(TAG, "version.code out of range");
        return ERROR_INVALID_ARGUMENT;
    }
    out_package.version_code = version_code;

    std::string target_sdk;
    if (!app_package_manifest_get_value(properties, "target.sdk", target_sdk)) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (!app_package_manifest_copy_bounded(out_package.target_sdk, sizeof(out_package.target_sdk), target_sdk)) {
        LOG_E(TAG, "target.sdk too long");
        return ERROR_INVALID_ARGUMENT;
    }

    auto device_id_iterator = properties.find("requires.device.id");
    if (device_id_iterator != properties.end()) {
        const std::string& device_id = device_id_iterator->second;
        if (!app_package_manifest_is_valid_device_id_list(device_id)) {
            LOG_E(TAG, "Invalid requires.device.id");
            return ERROR_INVALID_ARGUMENT;
        }
        if (!app_package_manifest_copy_bounded(out_package.requires_device_id, sizeof(out_package.requires_device_id), device_id)) {
            LOG_E(TAG, "requires.device.id too long");
            return ERROR_INVALID_ARGUMENT;
        }
    }

    // app.<index>.* blocks: 0-indexed, contiguous - the first missing "app.<index>.id" ends the list.
    size_t count = 0;
    while (properties.contains(std::format("app.{}.id", count))) {
        count++;
        // Bounded here, not just against the caller's bindings_capacity below: a caller that
        // sizes its own buffer off PackageManifest::app_manifest_count (see
        // app_package_manifest_parse_into()) would otherwise let a malicious/malformed manifest
        // demand an unbounded allocation.
        if (count > PACKAGE_MANIFEST_MAX_APP_MANIFESTS) {
            LOG_E(TAG, "Manifest declares more than %d apps", PACKAGE_MANIFEST_MAX_APP_MANIFESTS);
            return ERROR_BUFFER_OVERFLOW;
        }
    }

    out_package.app_manifest_count = static_cast<uint32_t>(count);

    // Catch a gap the count loop above would otherwise silently drop, e.g. app.0.* + app.3.*.
    for (const auto& [key, value] : properties) {
        auto index = parse_app_key_index(key);
        if (index.has_value() && *index >= count) {
            LOG_E(TAG, "Manifest declares %s but only %zu contiguous app(s) starting at app.0 were found", key.c_str(), count);
            return ERROR_INVALID_ARGUMENT;
        }
    }

    if (count == 0 || bindings_capacity == 0) {
        return ERROR_NONE;
    }
    if (count > bindings_capacity) {
        LOG_E(TAG, "Manifest declares %zu apps, capacity is %zu", count, bindings_capacity);
        return ERROR_BUFFER_OVERFLOW;
    }

    for (size_t i = 0; i < count; i++) {
        error_t result = parse_app_manifest(properties, i, out_bindings[i]);
        if (result != ERROR_NONE) {
            return result;
        }
    }

    return ERROR_NONE;
}
