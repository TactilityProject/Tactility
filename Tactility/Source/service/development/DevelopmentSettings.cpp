#ifdef ESP_PLATFORM
#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/Paths.h>
#include <Tactility/service/development/DevelopmentSettings.h>
#include <map>
#include <string>

#include <tactility/log.h>

namespace tt::service::development {

constexpr auto* TAG = "DevSettings";

static std::string getSettingsFilePath() {
    return getUserDataPath() + "/settings/development.properties";
}

constexpr auto* SETTINGS_KEY_ENABLE_ON_BOOT = "enableOnBoot";

struct DevelopmentSettings {
    bool enableOnBoot;
};

static bool load(DevelopmentSettings& settings) {
    auto settings_path = getSettingsFilePath();
    if (!file::isFile(settings_path)) {
        return false;
    }

    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(settings_path, map)) {
        return false;
    }

    if (!map.contains(SETTINGS_KEY_ENABLE_ON_BOOT)) {
        return false;
    }

    auto enable_on_boot_string = map[SETTINGS_KEY_ENABLE_ON_BOOT];
    settings.enableOnBoot = (enable_on_boot_string == "true");
    return true;
}

static bool save(const DevelopmentSettings& settings) {
    std::map<std::string, std::string> map;
    map[SETTINGS_KEY_ENABLE_ON_BOOT] = settings.enableOnBoot ? "true" : "false";
    auto settings_path = getSettingsFilePath();
    if (!file::findOrCreateParentDirectory(settings_path, 0755)) {
        LOG_E(TAG, "Failed to create parent dir for %s", settings_path.c_str());
        return false;
    }
    return file::savePropertiesFile(settings_path, map);
}

void setEnableOnBoot(bool enable) {
    DevelopmentSettings properties { .enableOnBoot = enable };
    if (!save(properties)) {
        LOG_E(TAG, "Failed to save %s", getSettingsFilePath().c_str());
    }
}

bool shouldEnableOnBoot() {
    DevelopmentSettings settings;
    if (!load(settings)) {
        return false;
    }
    return settings.enableOnBoot;
}
}

#endif // ESP_PLATFORM
