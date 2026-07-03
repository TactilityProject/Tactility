#include <Tactility/service/wifi/WifiSettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/Logger.h>
#include <Tactility/service/ServicePaths.h>
#include <Tactility/service/wifi/WifiPrivate.h>

namespace tt::service::wifi::settings {

static const auto LOGGER = Logger("WifiSettings");
constexpr auto* SETTINGS_KEY_ENABLE_ON_BOOT = "enableOnBoot";

struct WifiSettings {
    bool enableOnBoot;
};

static WifiSettings cachedSettings {
    .enableOnBoot = false
};

static bool cached = false;

static bool hasWifiSettingsFile(std::shared_ptr<ServiceContext> context) {
    std::string settings_path = context->getPaths()->getUserDataPath("settings.properties");
    return file::isFile(settings_path);
}

static bool load(std::shared_ptr<ServiceContext> context, WifiSettings& settings) {
    std::map<std::string, std::string> map;
    std::string settings_path = context->getPaths()->getUserDataPath("settings.properties");
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

static bool save(std::shared_ptr<ServiceContext> context, const WifiSettings& settings) {
    std::map<std::string, std::string> map;
    map[SETTINGS_KEY_ENABLE_ON_BOOT] = settings.enableOnBoot ? "true" : "false";
    std::string settings_path = context->getPaths()->getUserDataPath("settings.properties");
    if (!file::findOrCreateParentDirectory(settings_path, 0755)) {
        LOGGER.error("Failed to create {}", settings_path);
        return false;
    }
    return file::savePropertiesFile(settings_path, map);
}

WifiSettings getCachedOrLoad() {
    if (!cached) {
        auto context = findServiceContext();
        if (context && hasWifiSettingsFile(context)) {
            if (load(context, cachedSettings)) {
                cached = true;
            } else {
                LOGGER.info("Failed to load settings, using defaults");
            }
        }
    }

    return cachedSettings;
}

void setEnableOnBoot(bool enable) {
    cachedSettings.enableOnBoot = enable;
    auto context = findServiceContext();
    if (context && !save(context, cachedSettings)) {
        LOGGER.error("Failed to save settings");
    }
}

bool shouldEnableOnBoot() {
    return getCachedOrLoad().enableOnBoot;
}

} // namespace
