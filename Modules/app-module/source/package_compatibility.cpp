// SPDX-License-Identifier: Apache-2.0
#include <app/package_manifest.h>

#include <tactility/memory.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <cstring>

namespace {

// requires_device_id is a comma-separated list without spaces (see app_package_manifest_is_valid_device_id_list())
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
    if (manifest->requires_device_id[0] != '\0' && !is_device_listed(manifest->requires_device_id, CONFIG_TT_DEVICE_ID)) {
        return false;
    }
    // Internal and external memory together, as an app's allocations can come from either
    const uint64_t total_ram = static_cast<uint64_t>(memory_heap_total()) + memory_external_total();
    const uint64_t required_ram = static_cast<uint64_t>(manifest->requires_ram) * 1024 * 1024;
    return total_ram >= required_ram;
}

} // extern "C"
