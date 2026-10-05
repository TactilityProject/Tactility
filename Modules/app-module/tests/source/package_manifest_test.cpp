// SPDX-License-Identifier: Apache-2.0
#include "doctest.h"

#include <app/package_manifest.h>
#include <app/private/package_manifest_parsing.h>

#include <cstring>
#include <map>
#include <string>

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
    package.requires_ram = 1;
    CHECK(app_package_manifest_is_compatible(&package));
}
