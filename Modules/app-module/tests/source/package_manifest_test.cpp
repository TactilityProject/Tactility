// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"

#include <app/package_manifest.h>
#include <app/private/package_manifest_parsing.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

std::map<std::string, std::string> minimal_v3_properties() {
    return {
        { "manifest.version", "0.3" },
        { "id", "test.package" },
        { "version.name", "1.0.0" },
        { "version.code", "1" },
        { "target.sdk", "0.8.0" },
        { "app.0.id", "test.package.app" },
        { "app.0.name", "App" },
        { "app.0.binary", "app" },
    };
}

error_t parse(const std::map<std::string, std::string>& properties, PackageManifest& package) {
    AppManifestBinding bindings[1];
    return package_manifest_parse_v3(properties, package, bindings, 1);
}

error_t parse_app_flags(const std::map<std::string, std::string>& properties, uint8_t& out_flags) {
    PackageManifest package {};
    AppManifestBinding bindings[1] {};
    const error_t result = package_manifest_parse_v3(properties, package, bindings, 1);
    out_flags = bindings[0].manifest.flags;
    return result;
}

} // namespace

TEST_CASE("package manifest v3: requires.ram defaults to 0") {
    PackageManifest package {};
    REQUIRE_EQ(parse(minimal_v3_properties(), package), ERROR_NONE);
    CHECK_EQ(package.requires_ram, 0);
}

TEST_CASE("package manifest v3: requires.ram parses whole megabytes with an M suffix") {
    auto properties = minimal_v3_properties();
    properties["requires.ram"] = "2M";
    PackageManifest package {};
    REQUIRE_EQ(parse(properties, package), ERROR_NONE);
    CHECK_EQ(package.requires_ram, 2);
}

TEST_CASE("package manifest v3: an invalid requires.ram rejects the manifest") {
    for (const char* value : { "2", "2m", "M", "256M", "-1M", "2MB", "" }) {
        CAPTURE(value);
        auto properties = minimal_v3_properties();
        properties["requires.ram"] = value;
        PackageManifest package {};
        CHECK_EQ(parse(properties, package), ERROR_INVALID_ARGUMENT);
    }
}

TEST_CASE("package manifest v3: cleanup defaults to disabled") {
    uint8_t flags = 0xFF;
    REQUIRE_EQ(parse_app_flags(minimal_v3_properties(), flags), ERROR_NONE);
    CHECK_EQ(flags & APP_MANIFEST_FLAG_CLEANUP, 0);
}

TEST_CASE("package manifest v3: cleanup=true sets the cleanup flag") {
    auto properties = minimal_v3_properties();
    properties["app.0.cleanup"] = "true";
    uint8_t flags = 0;
    REQUIRE_EQ(parse_app_flags(properties, flags), ERROR_NONE);
    CHECK_NE(flags & APP_MANIFEST_FLAG_CLEANUP, 0);

    properties["app.0.cleanup"] = "false";
    REQUIRE_EQ(parse_app_flags(properties, flags), ERROR_NONE);
    CHECK_EQ(flags & APP_MANIFEST_FLAG_CLEANUP, 0);
}

TEST_CASE("package manifest v3: an invalid cleanup rejects the manifest") {
    auto properties = minimal_v3_properties();
    properties["app.0.cleanup"] = "yes";
    uint8_t flags = 0;
    CHECK_EQ(parse_app_flags(properties, flags), ERROR_INVALID_ARGUMENT);
}

TEST_CASE("package manifest compatibility: requires_device_id must list this device, if set") {
    PackageManifest package {};
    strcpy(package.target_sdk, "0.8.0");
    CHECK(app_package_manifest_is_compatible(&package));

    strcpy(package.requires_device_id, CONFIG_TT_DEVICE_ID);
    CHECK(app_package_manifest_is_compatible(&package));

    strcpy(package.requires_device_id, "other-device," CONFIG_TT_DEVICE_ID);
    CHECK(app_package_manifest_is_compatible(&package));

    strcpy(package.requires_device_id, "other-device");
    CHECK_FALSE(app_package_manifest_is_compatible(&package));

    // A listed id that only starts with this device's id is a different device
    strcpy(package.requires_device_id, CONFIG_TT_DEVICE_ID "-v2");
    CHECK_FALSE(app_package_manifest_is_compatible(&package));
}

TEST_CASE("package manifest compatibility: requires_ram must fit in total RAM") {
    PackageManifest package {};
    strcpy(package.target_sdk, "0.8.0");
    package.requires_ram = 1;
    CHECK(app_package_manifest_is_compatible(&package));
}

TEST_CASE("package manifest compatibility: target_sdk can't be newer than the firmware") {
    PackageManifest package {};
    strcpy(package.target_sdk, TT_VERSION);
    CHECK(app_package_manifest_is_compatible(&package));

    // The tag is ignored
    strcpy(package.target_sdk, "0.8.0");
    CHECK(app_package_manifest_is_compatible(&package));

    strcpy(package.target_sdk, "0.0.1");
    CHECK(app_package_manifest_is_compatible(&package));

    strcpy(package.target_sdk, "99.0.0");
    CHECK_FALSE(app_package_manifest_is_compatible(&package));

    strcpy(package.target_sdk, "0.99.0");
    CHECK_FALSE(app_package_manifest_is_compatible(&package));

    strcpy(package.target_sdk, "0.8.99");
    CHECK_FALSE(app_package_manifest_is_compatible(&package));

    strcpy(package.target_sdk, "latest");
    CHECK_FALSE(app_package_manifest_is_compatible(&package));
}

TEST_CASE("package manifest compatibility: target_platforms must list this platform, if set") {
    PackageManifest package {};
    strcpy(package.target_sdk, "0.8.0");
    CHECK(app_package_manifest_is_compatible(&package));

    strcpy(package.target_platforms, "other-platform");
    CHECK_FALSE(app_package_manifest_is_compatible(&package));

#ifdef ESP_PLATFORM
    strcpy(package.target_platforms, "other-platform," CONFIG_IDF_TARGET);
    CHECK(app_package_manifest_is_compatible(&package));
#endif
}

TEST_CASE("package manifest v3: target.platforms is optional") {
    PackageManifest package {};
    REQUIRE_EQ(parse(minimal_v3_properties(), package), ERROR_NONE);
    CHECK_EQ(std::string(package.target_platforms), "");

    auto properties = minimal_v3_properties();
    properties["target.platforms"] = "esp32,esp32s3";
    REQUIRE_EQ(parse(properties, package), ERROR_NONE);
    CHECK_EQ(std::string(package.target_platforms), "esp32,esp32s3");
}

namespace {

error_t parse_module_binaries(const std::map<std::string, std::string>& properties, std::vector<std::string>& out_binaries) {
    return package_manifest_parse_module_binaries_v3(properties, out_binaries);
}

} // namespace

TEST_CASE("package manifest v3: no module entries yields an empty list") {
    std::vector<std::string> binaries { "stale" };
    REQUIRE_EQ(parse_module_binaries(minimal_v3_properties(), binaries), ERROR_NONE);
    CHECK(binaries.empty());
}

TEST_CASE("package manifest v3: module binaries are returned in index order") {
    auto properties = minimal_v3_properties();
    properties["module.0.binary"] = "first";
    properties["module.1.binary"] = "second.v2";
    std::vector<std::string> binaries;
    REQUIRE_EQ(parse_module_binaries(properties, binaries), ERROR_NONE);
    REQUIRE_EQ(binaries.size(), 2);
    CHECK_EQ(binaries[0], "first");
    CHECK_EQ(binaries[1], "second.v2");
}

TEST_CASE("package manifest v3: module entries do not count as apps") {
    auto properties = minimal_v3_properties();
    properties["module.0.binary"] = "first";
    PackageManifest package {};
    REQUIRE_EQ(parse(properties, package), ERROR_NONE);
    CHECK_EQ(package.app_manifest_count, 1);
}

TEST_CASE("package manifest v3: a gap in module entries rejects the manifest") {
    auto properties = minimal_v3_properties();
    properties["module.0.binary"] = "first";
    properties["module.2.binary"] = "third";
    std::vector<std::string> binaries;
    CHECK_EQ(parse_module_binaries(properties, binaries), ERROR_INVALID_ARGUMENT);
}

TEST_CASE("package manifest v3: an invalid module binary name rejects the manifest") {
    for (const char* value : { "", "a/b", "a b", "../x" }) {
        CAPTURE(value);
        auto properties = minimal_v3_properties();
        properties["module.0.binary"] = value;
        std::vector<std::string> binaries;
        CHECK_EQ(parse_module_binaries(properties, binaries), ERROR_INVALID_ARGUMENT);
    }
}
