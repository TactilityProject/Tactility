// SPDX-License-Identifier: Apache-2.0
#include <app/package_manifest.h>

#include <tactility/memory.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <cstdio>
#include <cstring>

namespace {

#ifdef ESP_PLATFORM
constexpr auto* THIS_PLATFORM = CONFIG_IDF_TARGET;
#else
constexpr auto* THIS_PLATFORM = TACTILITY_POSIX_PLATFORM;
#endif

struct Version {
    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;
};

// "major.minor.patch" with an optional "-tag" suffix, which is ignored
bool parse_version(const char* text, Version& version) {
    int consumed = 0;
    if (sscanf(text, "%u.%u.%u%n", &version.major, &version.minor, &version.patch, &consumed) != 3) {
        return false;
    }
    return text[consumed] == '\0' || text[consumed] == '-';
}

bool is_newer(const Version& left, const Version& right) {
    if (left.major != right.major) {
        return left.major > right.major;
    }
    if (left.minor != right.minor) {
        return left.minor > right.minor;
    }
    return left.patch > right.patch;
}

// Comma-separated list without spaces (see app_package_manifest_is_valid_device_id_list())
bool is_device_listed(const char* device_ids, const char* device_id) {
    const size_t device_id_length = strlen(device_id);
    const char* item = device_ids;
    while (*item != '\0') {
        const char* comma = strchr(item, ',');
        const size_t item_length = (comma != nullptr) ? static_cast<size_t>(comma - item) : strlen(item);
        if (item_length == device_id_length && strncmp(item, device_id, item_length) == 0) {
            return true;
        }
        if (comma == nullptr) {
            break;
        }
        item = comma + 1;
    }
    return false;
}

} // namespace

extern "C" {

bool app_package_manifest_is_compatible(const struct PackageManifest* const manifest) {
    // The SDK templates write "all" for apps that are built for every platform
    if (manifest->target_platforms[0] != '\0' && strcmp(manifest->target_platforms, "all") != 0 &&
        !is_device_listed(manifest->target_platforms, THIS_PLATFORM)) {
        return false;
    }
    // An app built for a newer SDK can use APIs this firmware doesn't have. Not every caller knows the SDK version.
    if (manifest->target_sdk[0] != '\0') {
        Version target_sdk;
        Version firmware;
        if (!parse_version(manifest->target_sdk, target_sdk) || !parse_version(TT_VERSION, firmware) || is_newer(target_sdk, firmware)) {
            return false;
        }
    }
    if (manifest->requires_device_id[0] != '\0' && !is_device_listed(manifest->requires_device_id, CONFIG_TT_DEVICE_ID)) {
        return false;
    }
    // Internal and external memory together, as an app's allocations can come from either
    const uint64_t total_ram = static_cast<uint64_t>(memory_heap_total()) + memory_external_total();
    const uint64_t required_ram = static_cast<uint64_t>(manifest->requires_ram) * 1024 * 1024;
    return total_ram >= required_ram;
}

} // extern "C"
