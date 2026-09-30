#include <Tactility/app/apphub/AppHubEntry.h>
#include <Tactility/file/File.h>
#include <Tactility/json/Reader.h>

#include <app/package_manifest.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <tactility/log.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace tt::app::apphub {

constexpr auto* TAG = "AppHubJson";

static bool parseEntry(const cJSON* object, AppHubEntry& entry) {
    const json::Reader reader(object);
    // Optional: absent in apps.json files from before these fields existed
    // Reset because parseJson() reuses entries from a previous parse
    entry.requiresDeviceId.clear();
    entry.requiresRam = 0;
    if (cJSON_HasObjectItem(object, "requiresDeviceId") && !reader.readStringArray("requiresDeviceId", entry.requiresDeviceId)) {
        return false;
    }
    if (cJSON_HasObjectItem(object, "requiresRam") && !reader.readInt32("requiresRam", entry.requiresRam)) {
        return false;
    }
    return reader.readString("appId", entry.appId) &&
         reader.readString("appVersionName", entry.appVersionName) &&
         reader.readInt32("appVersionCode", entry.appVersionCode) &&
         reader.readString("appName", entry.appName) &&
         reader.readString("appDescription", entry.appDescription) &&
         reader.readString("targetSdk", entry.targetSdk) &&
         reader.readString("file", entry.file) &&
         reader.readStringArray("targetPlatforms", entry.targetPlatforms);
}

bool parseJson(const std::string& filePath, AppHubEntryList& entries) {
    auto data = file::readString(filePath);
    if (data == nullptr) {
        LOG_E(TAG, "Failed to read %s", filePath.c_str());
        return false;
    }

    auto data_ptr = reinterpret_cast<const char*>(data.get());
    auto* json = cJSON_Parse(data_ptr);
    if (json == nullptr) {
        LOG_E(TAG, "Failed to parse %s", filePath.c_str());
        return false;
    }

    const cJSON* apps_json = cJSON_GetObjectItemCaseSensitive(json, "apps");
    if (!cJSON_IsArray(apps_json)) {
        cJSON_Delete(json);
        LOG_E(TAG, "apps is not an array");
        return false;
    }

    auto apps_size = cJSON_GetArraySize(apps_json);
    entries.resize(apps_size);
    for (int i = 0; i < apps_size; ++i) {
        auto& entry = entries.at(i);
        auto* entry_json = cJSON_GetArrayItem(apps_json, i);
        if (!parseEntry(entry_json, entry)) {
            LOG_E(TAG, "Failed to read entry");
            cJSON_Delete(json);
            return false;
        }
    }

    cJSON_Delete(json);
    return true;
}

bool isCompatible(const AppHubEntry& entry) {
    // The simulator isn't a real MCU target, so it has no platform to match against
#ifdef ESP_PLATFORM
    if (!entry.targetPlatforms.empty() &&
        std::ranges::find(entry.targetPlatforms, std::string_view(CONFIG_IDF_TARGET)) == entry.targetPlatforms.end()) {
        return false;
    }
#endif

    // Same device and RAM rules as for installed packages
    PackageManifest package {};
    std::string deviceIds;
    for (const auto& deviceId : entry.requiresDeviceId) {
        if (!deviceIds.empty()) {
            deviceIds += ',';
        }
        deviceIds += deviceId;
    }
    if (deviceIds.size() >= sizeof(package.requires_device_id)) {
        LOG_W(TAG, "%s: requiresDeviceId too long", entry.appId.c_str());
        return false;
    }
    strcpy(package.requires_device_id, deviceIds.c_str());
    if (entry.requiresRam < 0 || entry.requiresRam > UINT8_MAX) {
        LOG_W(TAG, "%s: invalid requiresRam %d", entry.appId.c_str(), static_cast<int>(entry.requiresRam));
        return false;
    }
    package.requires_ram = static_cast<uint8_t>(entry.requiresRam);
    return app_package_manifest_is_compatible(&package);
}

}
