#include <Tactility/settings/AppearanceSettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>

#include <app/paths.h>

#include <cstdlib>
#include <map>

namespace tt::settings::appearance {

constexpr auto* SETTINGS_KEY_FONT_SIZE = "fontSize";
constexpr auto* SETTINGS_KEY_REGULAR_FONT_PATH = "regularFontPath";
constexpr auto* SETTINGS_KEY_MONO_FONT_PATH = "monoFontPath";

static std::string getSettingsFilePath() {
    char path[256];
    if (app_paths_get_user_data_path("tactility.appearance", "appearance.properties", path, sizeof(path)) != ERROR_NONE) {
        return "";
    }
    return path;
}

bool load(AppearanceSettings& settings) {
    const auto settings_path = getSettingsFilePath();
    if (settings_path.empty() || !file::isFile(settings_path)) {
        return false;
    }

    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(settings_path, map)) {
        return false;
    }

    settings = AppearanceSettings();
    if (const auto entry = map.find(SETTINGS_KEY_FONT_SIZE); entry != map.end()) {
        settings.fontSize = static_cast<uint16_t>(std::strtoul(entry->second.c_str(), nullptr, 10));
    }
    if (const auto entry = map.find(SETTINGS_KEY_REGULAR_FONT_PATH); entry != map.end()) {
        settings.regularFontPath = entry->second;
    }
    if (const auto entry = map.find(SETTINGS_KEY_MONO_FONT_PATH); entry != map.end()) {
        settings.monoFontPath = entry->second;
    }
    return true;
}

AppearanceSettings loadOrGetDefault() {
    AppearanceSettings settings;
    if (!load(settings)) {
        settings = AppearanceSettings();
    }
    return settings;
}

bool save(const AppearanceSettings& settings) {
    const auto settings_path = getSettingsFilePath();
    if (settings_path.empty() || !file::findOrCreateParentDirectory(settings_path, 0755)) {
        return false;
    }
    std::map<std::string, std::string> map;
    map[SETTINGS_KEY_FONT_SIZE] = std::to_string(settings.fontSize);
    map[SETTINGS_KEY_REGULAR_FONT_PATH] = settings.regularFontPath;
    map[SETTINGS_KEY_MONO_FONT_PATH] = settings.monoFontPath;
    return file::savePropertiesFile(settings_path, map);
}

}
