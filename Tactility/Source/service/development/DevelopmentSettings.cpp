#ifdef ESP_PLATFORM
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/Logger.h>
#include <Tactility/Paths.h>
#include <Tactility/service/development/DevelopmentSettings.h>
#include <map>
#include <string>

namespace tt::service::development {

static const auto LOGGER = Logger("DevSettings");

static std::string getSettingsFilePath() {
    return getUserDataPath() + "/settings/development.properties";
}

constexpr auto* SETTINGS_KEY_ENABLE_ON_BOOT = "enableOnBoot";

struct DevelopmentSettings {
    bool enableOnBoot;
};

static bool load(DevelopmentSettings& settings) {
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(getSettingsFilePath(), map)) {
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
    return file::savePropertiesFile(getSettingsFilePath(), map);
}

void setEnableOnBoot(bool enable) {
    DevelopmentSettings properties { .enableOnBoot = enable };
    if (!save(properties)) {
        LOGGER.error("Failed to save {}", getSettingsFilePath());
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
