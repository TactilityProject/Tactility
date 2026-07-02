#include <Tactility/app/AppManifestParsing.h>
#include <Tactility/app/AppManifestParsingInternal.h>

#include <Tactility/Logger.h>

namespace tt::app {

static const auto LOGGER = Logger("AppManifestV1");

bool parseManifestV1(const std::map<std::string, std::string>& map, AppManifest& manifest) {
    // [manifest]

    std::string manifest_version;
    if (!getValueFromManifest(map, "[manifest]version", manifest_version)) {
        return false;
    }

    if (!isValidManifestVersion(manifest_version)) {
        LOGGER.error("Invalid version");
        return false;
    }

    // [app]

    if (!getValueFromManifest(map, "[app]id", manifest.appId)) {
        return false;
    }

    if (!isValidId(manifest.appId)) {
        LOGGER.error("Invalid app id");
        return false;
    }

    if (!getValueFromManifest(map, "[app]name", manifest.appName)) {
        return false;
    }

    if (!isValidName(manifest.appName)) {
        LOGGER.error("Invalid app name");
        return false;
    }

    if (!getValueFromManifest(map, "[app]versionName", manifest.appVersionName)) {
        return false;
    }

    if (!isValidAppVersionName(manifest.appVersionName)) {
        LOGGER.error("Invalid app version name");
        return false;
    }

    std::string version_code_string;
    if (!getValueFromManifest(map, "[app]versionCode", version_code_string)) {
        return false;
    }

    if (!isValidAppVersionCode(version_code_string)) {
        LOGGER.error("Invalid app version code");
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
