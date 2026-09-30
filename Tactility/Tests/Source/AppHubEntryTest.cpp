#include "doctest.h"

#include <Tactility/app/apphub/AppHubEntry.h>

#include <tactility/filesystem/fs.h>
#include <tactility/paths.h>

#include <cstdio>
#include <string>

using namespace tt::app::apphub;

namespace {

std::string writeAppsJson(const char* contents) {
    char temp[FILE_MAX_PATH_STRING_LENGTH];
    REQUIRE_EQ(paths_get_temp_path(temp, sizeof(temp)), ERROR_NONE);
    REQUIRE_EQ(directory_make(temp, true), ERROR_NONE);
    const std::string path = std::string(temp) + "/apphub_test_apps.json";
    FILE* file = fopen(path.c_str(), "w");
    REQUIRE_NE(file, nullptr);
    fputs(contents, file);
    fclose(file);
    return path;
}

AppHubEntry entryWith(std::vector<std::string> requiresDeviceId, int32_t requiresRam) {
    AppHubEntry entry {};
    entry.requiresDeviceId = std::move(requiresDeviceId);
    entry.requiresRam = requiresRam;
    return entry;
}

} // namespace

TEST_CASE("app hub: requiresDeviceId and requiresRam are parsed, and optional") {
    const auto path = writeAppsJson(R"({"apps": [
        {"appId": "a", "appVersionName": "1", "appVersionCode": 1, "appName": "A", "appDescription": "", "targetSdk": "0.8.0", "targetPlatforms": [], "file": "a-1.app",
         "requiresDeviceId": ["m5stack-tab5", "lilygo-tdeck"], "requiresRam": 2},
        {"appId": "b", "appVersionName": "1", "appVersionCode": 1, "appName": "B", "appDescription": "", "targetSdk": "0.8.0", "targetPlatforms": [], "file": "b-1.app"}
    ]})");
    AppHubEntryList entries;
    REQUIRE(parseJson(path, entries));
    REQUIRE_EQ(entries.size(), 2u);
    CHECK_EQ(entries[0].requiresDeviceId, std::vector<std::string> { "m5stack-tab5", "lilygo-tdeck" });
    CHECK_EQ(entries[0].requiresRam, 2);
    CHECK(entries[1].requiresDeviceId.empty());
    CHECK_EQ(entries[1].requiresRam, 0);
    remove(path.c_str());
}

TEST_CASE("app hub: reparsing into the same list resets optional requirements") {
    AppHubEntryList entries;
    const auto restricted = writeAppsJson(R"({"apps": [
        {"appId": "a", "appVersionName": "1", "appVersionCode": 1, "appName": "A", "appDescription": "", "targetSdk": "0.8.0", "targetPlatforms": [], "file": "a-1.app",
         "requiresDeviceId": ["m5stack-tab5"], "requiresRam": 2}
    ]})");
    REQUIRE(parseJson(restricted, entries));
    const auto unrestricted = writeAppsJson(R"({"apps": [
        {"appId": "b", "appVersionName": "1", "appVersionCode": 1, "appName": "B", "appDescription": "", "targetSdk": "0.8.0", "targetPlatforms": [], "file": "b-1.app"}
    ]})");
    REQUIRE(parseJson(unrestricted, entries));
    REQUIRE_EQ(entries.size(), 1u);
    CHECK(entries[0].requiresDeviceId.empty());
    CHECK_EQ(entries[0].requiresRam, 0);
    remove(unrestricted.c_str());
}

TEST_CASE("app hub: a fractional requiresRam rejects the entry") {
    const auto path = writeAppsJson(R"({"apps": [
        {"appId": "a", "appVersionName": "1", "appVersionCode": 1, "appName": "A", "appDescription": "", "targetSdk": "0.8.0", "targetPlatforms": [], "file": "a-1.app",
         "requiresRam": 1.5}
    ]})");
    AppHubEntryList entries;
    CHECK_FALSE(parseJson(path, entries));
    remove(path.c_str());
}

TEST_CASE("app hub: isCompatible applies the device and RAM requirements") {
    CHECK(isCompatible(entryWith({}, 0)));
    CHECK(isCompatible(entryWith({ "other-device", CONFIG_TT_DEVICE_ID }, 1)));
    CHECK_FALSE(isCompatible(entryWith({ "other-device" }, 0)));
    CHECK_FALSE(isCompatible(entryWith({}, 256)));
}
