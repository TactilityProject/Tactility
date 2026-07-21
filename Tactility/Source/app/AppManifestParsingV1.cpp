#include <Tactility/app/AppManifestParsing.h>
#include <Tactility/app/AppManifestParsingInternal.h>

#include <tactility/log.h>

namespace tt::app {

constexpr auto* TAG = "AppManifestV1";

bool parseManifestV1(const std::map<std::string, std::string>& map, AppManifest& manifest) {
    // [manifest]

    std::string manifest_version;
    if (!getValueFromManifest(map, "[manifest]version", manifest_version)) {
        return false;
    }

    if (!isValidManifestVersion(manifest_version)) {
        LOG_E(TAG, "Invalid version");
        return false;
    }

    // [app]

    if (!getValueFromManifest(map, "[app]id", manifest.appId)) {
        return false;
    }

    if (!isValidId(manifest.appId)) {
        LOG_E(TAG, "Invalid app id");
        return false;
    }

    if (!getValueFromManifest(map, "[app]name", manifest.appName)) {
        return false;
    }

    if (!isValidName(manifest.appName)) {
        LOG_E(TAG, "Invalid app name");
        return false;
    }

    if (!getValueFromManifest(map, "[app]versionName", manifest.appVersionName)) {
        return false;
    }

    if (!isValidAppVersionName(manifest.appVersionName)) {
        LOG_E(TAG, "Invalid app version name");
        return false;
    }

    std::string version_code_string;
    if (!getValueFromManifest(map, "[app]versionCode", version_code_string)) {
        return false;
    }

    if (!isValidAppVersionCode(version_code_string)) {
        LOG_E(TAG, "Invalid app version code");
        return false;
    }

    manifest.appVersionCode = std::stoull(version_code_string);

    // [target]

    if (!getValueFromManifest(map, "[target]sdk", manifest.targetSdk)) {
        return false;
    }

    if (!getValueFromManifest(map, "[target]platforms", manifest.targetPlatforms)) {
        return false;
    }

    return true;
}

}
