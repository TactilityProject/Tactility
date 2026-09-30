#pragma once

#include <TactilityCpp/Allocator.h>

#include <string>
#include <vector>

namespace tt::app::apphub {

struct AppHubEntry {
    std::string appId;
    std::string appVersionName;
    int32_t appVersionCode;
    std::string appName;
    std::string appDescription;
    std::string targetSdk;
    std::vector<std::string> targetPlatforms;
    /** Device ids the app is restricted to (manifest "requires.device.id"). Empty means unrestricted. */
    std::vector<std::string> requiresDeviceId;
    /** RAM in megabytes the app needs (manifest "requires.ram"). 0 means no requirement. */
    int32_t requiresRam = 0;
    std::string file;
};

// The top-level entries buffer prefers PSRAM/SPIRAM via OptExternalAllocator; individual
// AppHubEntry string/vector members still use the default (internal-RAM) allocator.
using AppHubEntryList = std::vector<AppHubEntry, OptExternalAllocator<AppHubEntry>>;

bool parseJson(const std::string& filePath, AppHubEntryList& entries);

/** @return true if this device can install and run the entry's app (target platform, device and RAM requirements) */
bool isCompatible(const AppHubEntry& entry);

}