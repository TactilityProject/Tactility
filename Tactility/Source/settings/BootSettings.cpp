#include <Tactility/MountPoints.h>
#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/settings/BootSettings.h>

#include <Tactility/Paths.h>
#include <format>
#include <string>

namespace tt::settings {

constexpr auto* PROPERTIES_FILE_FORMAT = "{}/settings/boot.properties";
constexpr auto* PROPERTIES_KEY_LAUNCHER_APP_ID = "launcherAppId";
constexpr auto* PROPERTIES_KEY_AUTO_START_APP_ID = "autoStartAppId";

static std::string getPropertiesFilePath() {
    return std::format(PROPERTIES_FILE_FORMAT, getUserDataPath());
}

bool loadBootSettings(BootSettings& properties) {
    const std::string path = getPropertiesFilePath();
    if (!file::isFile(path)) {
        return false;
    }

    if (!file::loadPropertiesFile(path, [&properties](auto& key, auto& value) {
        if (key == PROPERTIES_KEY_AUTO_START_APP_ID) {
            properties.autoStartAppId = value;
        } else if (key == PROPERTIES_KEY_LAUNCHER_APP_ID) {
            properties.launcherAppId = value;
        }
    })) {
        return false;
    }

    return true;
}

}
