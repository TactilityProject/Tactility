#include <Tactility/MountPoints.h>
#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/hal/sdcard/SdCardDevice.h>
#include <Tactility/Logger.h>
#include <Tactility/settings/BootSettings.h>

#include <format>
#include <string>
#include <vector>

namespace tt::settings {

constexpr auto* TAG = "BootSettings";

constexpr auto* PROPERTIES_FILE_FORMAT = "{}/settings/boot.properties";
constexpr auto* PROPERTIES_KEY_LAUNCHER_APP_ID = "launcherAppId";
constexpr auto* PROPERTIES_KEY_AUTO_START_APP_ID = "autoStartAppId";

static std::string getPropertiesFilePath() {
    const auto sdcards = hal::findDevices<hal::sdcard::SdCardDevice>(hal::Device::Type::SdCard);
    for (auto& sdcard : sdcards) {
        std::string path = std::format(PROPERTIES_FILE_FORMAT, sdcard->getMountPath());
        if (file::isFile(path)) {
            return path;
        }
    }
    return std::format(PROPERTIES_FILE_FORMAT, file::MOUNT_POINT_DATA);
}

bool loadBootSettings(BootSettings& properties) {
    const std::string path = getPropertiesFilePath();
    if (!file::loadPropertiesFile(path, [&properties](auto& key, auto& value) {
        if (key == PROPERTIES_KEY_AUTO_START_APP_ID) {
            properties.autoStartAppId = value;
        } else if (key == PROPERTIES_KEY_LAUNCHER_APP_ID) {
            properties.launcherAppId = value;
        }
    })) {
        LOG_I(TAG, "No settings at %s", path.c_str());
        return false;
    }

    return true;
}

}
