#include "../../TactilityCore/Source/TestFile.h"
#include "../../Tactility/Private/Tactility/app/AppManifestParsing.h"
#include "../../Tactility/Private/Tactility/app/AppManifestParsingInternal.h"

#include "doctest.h"

using namespace tt;
using namespace tt::app;

TEST_CASE("parseManifest() should parse a V1 (sectioned) manifest.properties file") {
    TestFile file("test-manifest-v1.properties");
    file.writeData(
        "[manifest]\n"
        "version=0.1\n"
        "[target]\n"
        "sdk=0.0.0\n"
        "platforms=esp32,esp32s3,esp32c6,esp32p4\n"
        "[app]\n"
        "id=one.tactility.sdktest\n"
        "versionName=0.1.0\n"
        "versionCode=1\n"
        "name=SDK Test\n"
    );

    AppManifest manifest;
    CHECK_EQ(parseManifest(file.getPath(), manifest), true);
    CHECK_EQ(manifest.targetSdk, "0.0.0");
    CHECK_EQ(manifest.targetPlatforms, "esp32,esp32s3,esp32c6,esp32p4");
    CHECK_EQ(manifest.appId, "one.tactility.sdktest");
    CHECK_EQ(manifest.appName, "SDK Test");
    CHECK_EQ(manifest.appVersionName, "0.1.0");
    CHECK_EQ(manifest.appVersionCode, 1);
}

TEST_CASE("parseManifest() should parse a V2 (flat) manifest.properties file") {
    TestFile file("test-manifest-v2.properties");
    file.writeData(
        "manifest.version=0.1\n"
        "target.sdk=0.0.0\n"
        "target.platforms=esp32,esp32s3,esp32c6,esp32p4\n"
        "app.id=one.tactility.sdktest\n"
        "app.version.name=0.1.0\n"
        "app.version.code=1\n"
        "app.name=SDK Test\n"
    );

    AppManifest manifest;
    CHECK_EQ(parseManifest(file.getPath(), manifest), true);
    CHECK_EQ(manifest.targetSdk, "0.0.0");
    CHECK_EQ(manifest.targetPlatforms, "esp32,esp32s3,esp32c6,esp32p4");
    CHECK_EQ(manifest.appId, "one.tactility.sdktest");
    CHECK_EQ(manifest.appName, "SDK Test");
    CHECK_EQ(manifest.appVersionName, "0.1.0");
    CHECK_EQ(manifest.appVersionCode, 1);
}

TEST_CASE("parseManifestV1() should fail when a required key is missing") {
    std::map<std::string, std::string> properties = {
        {"[manifest]version", "0.1"},
        {"[app]id", "one.tactility.sdktest"},
        {"[app]name", "SDK Test"},
        {"[app]versionName", "0.1.0"},
        {"[app]versionCode", "1"},
        // Missing [target]sdk
        {"[target]platforms", "esp32"},
    };
    AppManifest manifest;
    CHECK_EQ(parseManifestV1(properties, manifest), false);
}

TEST_CASE("parseManifestV2() should fail when the app id is invalid") {
    std::map<std::string, std::string> properties = {
        {"manifest.version", "0.1"},
        {"app.id", "abc"}, // too short (isValidId requires >= 5 chars)
        {"app.name", "SDK Test"},
        {"app.version.name", "0.1.0"},
        {"app.version.code", "1"},
        {"target.sdk", "0.0.0"},
        {"target.platforms", "esp32"},
    };
    AppManifest manifest;
    CHECK_EQ(parseManifestV2(properties, manifest), false);
}
