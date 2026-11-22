#include "doctest.h"

#include <app_hub/app_hub_catalog.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string writeAppsJson(const char* contents) {
    const std::string path = "/tmp/tactility_app_hub_catalog_test.json";
    FILE* file = fopen(path.c_str(), "w");
    REQUIRE_NE(file, nullptr);
    fputs(contents, file);
    fclose(file);
    return path;
}

const std::string CATALOG_PATH = "/tmp/tactility_app_hub_catalog_test.txt";

std::vector<AppHubCatalogEntry> readCatalog() {
    std::vector<AppHubCatalogEntry> entries;
    REQUIRE(app_hub_catalog_for_each(CATALOG_PATH.c_str(), [](const AppHubCatalogEntry& entry, void* context) {
        static_cast<std::vector<AppHubCatalogEntry>*>(context)->push_back(entry);
        return true;
    }, &entries));
    return entries;
}

AppHubCatalogDetails detailsWith(std::vector<std::string> requiresDeviceId, int32_t requiresRam) {
    AppHubCatalogDetails details {};
    details.requires_device_id = std::move(requiresDeviceId);
    details.requires_ram = requiresRam;
    return details;
}

/** Converts a list page into a fresh catalog file */
bool convert(const std::string& jsonPath, int32_t& pageCount) {
    remove(CATALOG_PATH.c_str());
    return app_hub_catalog_append_page(jsonPath.c_str(), CATALOG_PATH.c_str(), pageCount);
}

} // namespace

TEST_CASE("app hub: converted entries keep all fields") {
    const auto path = writeAppsJson(R"({"pageCount": 1, "apps": [
        {"id": "a", "version": 12, "name": "A", "description": "Tab\there"},
        {"id": "b", "version": 1, "name": "B", "description": ""}
    ]})");
    int32_t pageCount = 0;
    REQUIRE(convert(path, pageCount));
    CHECK_EQ(pageCount, 1);
    const auto entries = readCatalog();
    REQUIRE_EQ(entries.size(), 2u);
    CHECK_EQ(entries[0].id, "a");
    CHECK_EQ(entries[0].version_code, 12);
    CHECK_EQ(entries[0].name, "A");
    CHECK_EQ(entries[0].short_description, "Tab here");
    CHECK_EQ(entries[1].id, "b");
    remove(path.c_str());
    remove(CATALOG_PATH.c_str());
}

TEST_CASE("app hub: pages are appended and the page count is returned") {
    const auto first = writeAppsJson(R"({"page": 1, "pageCount": 2, "apps": [
        {"id": "a", "version": 1, "name": "A", "description": ""}
    ]})");
    int32_t pageCount = 0;
    REQUIRE(convert(first, pageCount));
    CHECK_EQ(pageCount, 2);
    const auto second = writeAppsJson(R"({"page": 2, "pageCount": 2, "apps": [
        {"id": "b", "version": 1, "name": "B", "description": ""}
    ]})");
    REQUIRE(app_hub_catalog_append_page(second.c_str(), CATALOG_PATH.c_str(), pageCount));
    CHECK_EQ(readCatalog().size(), 2u);
    remove(first.c_str());
    remove(second.c_str());
    remove(CATALOG_PATH.c_str());
}

TEST_CASE("app hub: a page without a valid page count fails") {
    for (const char* json : { R"({"apps": []})", R"({"pageCount": 0, "apps": []})", R"({"pageCount": "2", "apps": []})" }) {
        const auto path = writeAppsJson(json);
        int32_t pageCount = 0;
        CHECK_FALSE(convert(path, pageCount));
        remove(path.c_str());
    }
}

TEST_CASE("app hub: an app has an icon unless the list says it hasn't") {
    const auto path = writeAppsJson(R"({"pageCount": 1, "apps": [
        {"id": "a", "version": 1, "name": "A", "description": ""},
        {"id": "b", "version": 1, "name": "B", "description": "", "icon": false},
        {"id": "c", "version": 1, "name": "C", "description": "", "icon": true}
    ]})");
    int32_t pageCount = 0;
    REQUIRE(convert(path, pageCount));
    const auto entries = readCatalog();
    REQUIRE_EQ(entries.size(), 3u);
    CHECK(entries[0].has_icon);
    CHECK_FALSE(entries[1].has_icon);
    CHECK(entries[2].has_icon);
    remove(path.c_str());

    const auto invalid = writeAppsJson(R"({"pageCount": 1, "apps": [{"id": "a", "version": 1, "name": "A", "description": "", "icon": "no"}]})");
    CHECK_FALSE(convert(invalid, pageCount));
    remove(invalid.c_str());
    remove(CATALOG_PATH.c_str());
}

TEST_CASE("app hub: details keep all fields") {
    const auto path = writeAppsJson(R"({"appId": "a", "appVersionName": "1.2", "appDescription": "Long", "targetSdk": "0.8.0", "targetPlatforms": ["esp32", "esp32s3"], "file": "a-1.app",
        "requiresDeviceId": ["m5stack-tab5", "lilygo-tdeck"], "requiresRam": 2})");
    AppHubCatalogDetails details;
    REQUIRE(app_hub_catalog_read_details(path.c_str(), details));
    CHECK_EQ(details.version_name, "1.2");
    CHECK_EQ(details.description, "Long");
    CHECK_EQ(details.target_sdk, "0.8.0");
    CHECK_EQ(details.target_platforms, std::vector<std::string> { "esp32", "esp32s3" });
    CHECK_EQ(details.file, "a-1.app");
    CHECK_EQ(details.requires_device_id, std::vector<std::string> { "m5stack-tab5", "lilygo-tdeck" });
    CHECK_EQ(details.requires_ram, 2);
    remove(path.c_str());
}

TEST_CASE("app hub: a catalog entry is found by id") {
    const auto path = writeAppsJson(R"({"pageCount": 1, "apps": [
        {"id": "a", "version": 1, "name": "A", "description": ""},
        {"id": "b", "version": 1, "name": "B", "description": ""}
    ]})");
    int32_t pageCount = 0;
    REQUIRE(convert(path, pageCount));
    AppHubCatalogEntry entry;
    REQUIRE(app_hub_catalog_find(CATALOG_PATH.c_str(), "b", entry));
    CHECK_EQ(entry.name, "B");
    CHECK_FALSE(app_hub_catalog_find(CATALOG_PATH.c_str(), "c", entry));
    remove(path.c_str());
    remove(CATALOG_PATH.c_str());
}

TEST_CASE("app hub: a visitor can stop the visit") {
    const auto path = writeAppsJson(R"({"pageCount": 1, "apps": [
        {"id": "a", "version": 1, "name": "A", "description": ""},
        {"id": "b", "version": 1, "name": "B", "description": ""}
    ]})");
    int32_t pageCount = 0;
    REQUIRE(convert(path, pageCount));
    int visited = 0;
    CHECK(app_hub_catalog_for_each(CATALOG_PATH.c_str(), [](const AppHubCatalogEntry&, void* context) {
        ++*static_cast<int*>(context);
        return false;
    }, &visited));
    CHECK_EQ(visited, 1);
    remove(path.c_str());
    remove(CATALOG_PATH.c_str());
}

TEST_CASE("app hub: a page with an invalid entry fails") {
    const auto path = writeAppsJson(R"({"pageCount": 1, "apps": [{"id": "a", "version": 1.5, "name": "A", "description": ""}]})");
    int32_t pageCount = 0;
    CHECK_FALSE(convert(path, pageCount));
    const auto tooLong = writeAppsJson((R"({"pageCount": 1, "apps": [{"id": "a", "version": 1, "name": "A", "description": ")" + std::string(2000, 'x') + R"("}]})").c_str());
    CHECK_FALSE(convert(tooLong, pageCount));
    remove(path.c_str());
    remove(tooLong.c_str());
    remove(CATALOG_PATH.c_str());
}

TEST_CASE("app hub: isCompatible applies the device and RAM requirements") {
    CHECK(app_hub_catalog_is_compatible(detailsWith({}, 0)));
    CHECK(app_hub_catalog_is_compatible(detailsWith({ "other-device", CONFIG_TT_DEVICE_ID }, 1)));
    CHECK_FALSE(app_hub_catalog_is_compatible(detailsWith({ "other-device" }, 0)));
    CHECK_FALSE(app_hub_catalog_is_compatible(detailsWith({}, 256)));
}

TEST_CASE("app hub: isCompatible requires this device's platform when the app lists platforms") {
    auto details = detailsWith({}, 0);
    CHECK(app_hub_catalog_is_compatible(details));
    details.target_platforms = { "some-other-platform" };
    CHECK_FALSE(app_hub_catalog_is_compatible(details));
    details.target_platforms = { "some-other-platform", app_hub_catalog_get_platform() };
    CHECK(app_hub_catalog_is_compatible(details));
}

TEST_CASE("app hub: entries keep the repository they came from") {
    const auto path = writeAppsJson(R"({"pageCount": 1, "apps": [
        {"id": "a", "version": 1, "name": "A", "description": "A"}
    ]})");
    remove(CATALOG_PATH.c_str());
    int32_t pageCount = 0;
    REQUIRE(app_hub_catalog_append_page(path.c_str(), CATALOG_PATH.c_str(), pageCount, 3));
    const auto entries = readCatalog();
    REQUIRE_EQ(entries.size(), 1u);
    CHECK_EQ(entries[0].repository, 3);
}

TEST_CASE("app hub: entries that are not accepted are left out") {
    const auto path = writeAppsJson(R"({"pageCount": 1, "apps": [
        {"id": "a", "version": 1, "name": "A", "description": "A"},
        {"id": "b", "version": 1, "name": "B", "description": "B"}
    ]})");
    remove(CATALOG_PATH.c_str());
    int32_t pageCount = 0;
    REQUIRE(app_hub_catalog_append_page(path.c_str(), CATALOG_PATH.c_str(), pageCount, 1, [](const AppHubCatalogEntry& entry, void*) {
        return entry.id != "a";
    }, nullptr));
    const auto entries = readCatalog();
    REQUIRE_EQ(entries.size(), 1u);
    CHECK_EQ(entries[0].id, "b");
}

TEST_CASE("app hub: a catalog line without a repository is from the first repository") {
    FILE* file = fopen(CATALOG_PATH.c_str(), "w");
    REQUIRE_NE(file, nullptr);
    fputs("a\t1\tA\tShort\t1\n", file);
    fclose(file);
    const auto entries = readCatalog();
    REQUIRE_EQ(entries.size(), 1u);
    CHECK_EQ(entries[0].repository, 0);
}

TEST_CASE("app hub: urls are built from the base url of a repository") {
    CHECK_EQ(app_hub_catalog_get_download_url("https://example.com/apps", "a-1.app").rfind("https://example.com/apps/", 0), 0u);
    CHECK_NE(app_hub_catalog_get_page_url("https://example.com/apps", 2).find("-2.json"), std::string::npos);
}
