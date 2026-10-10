#include <Tactility/settings/UsbHostSettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>

#include <tactility/paths.h>

#include <map>
#include <string>

namespace tt::settings::usbhost {

static bool getSettingsFilePath(std::string& outPath) {
    char root[128];
    if (paths_get_data_path(root, sizeof(root)) != ERROR_NONE) {
        return false;
    }
    outPath = std::string(root) + "/settings/usbhost.properties";
    return true;
}

constexpr auto* SETTINGS_KEY_ENABLED = "enabled";

static bool toBool(const std::map<std::string, std::string>& map, const char* key, bool defaultValue) {
    auto entry = map.find(key);
    if (entry == map.end()) {
        return defaultValue;
    }
    return (entry->second == "1" || entry->second == "true" || entry->second == "True");
}

bool load(UsbHostSettings& settings) {
    std::string settings_path;
    if (!getSettingsFilePath(settings_path) || !file::isFile(settings_path)) {
        return false;
    }

    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(settings_path, map)) {
        return false;
    }

    settings.enabled = toBool(map, SETTINGS_KEY_ENABLED, true);
    return true;
}

UsbHostSettings getDefault() {
    return UsbHostSettings { .enabled = true };
}

UsbHostSettings loadOrGetDefault() {
    UsbHostSettings settings;
    if (!load(settings)) {
        settings = getDefault();
    }
    return settings;
}

bool save(const UsbHostSettings& settings) {
    std::map<std::string, std::string> map;
    map[SETTINGS_KEY_ENABLED] = settings.enabled ? "1" : "0";

    std::string settings_path;
    if (!getSettingsFilePath(settings_path)) {
        return false;
    }
    if (!file::findOrCreateParentDirectory(settings_path, 0755)) {
        return false;
    }
    return file::savePropertiesFile(settings_path, map);
}

} // namespace tt::settings::usbhost
