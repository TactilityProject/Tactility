#include <Tactility/settings/AppearanceSettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>

#include <app/paths.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace tt::settings::appearance {

constexpr auto* SETTINGS_KEY_FONT_SIZE = "fontSize";
constexpr auto* SETTINGS_KEY_REGULAR_FONT_PATH = "regularFontPath";
constexpr auto* SETTINGS_KEY_MONO_FONT_PATH = "monoFontPath";
constexpr auto* SETTINGS_KEY_THEME_MODE = "themeMode";
constexpr auto* SETTINGS_KEY_MONO_THEME = "monoTheme";
constexpr auto* SETTINGS_KEY_ANIMATIONS = "animations";
constexpr auto* SETTINGS_KEY_PRIMARY_COLOR = "primaryColor";
constexpr auto* SETTINGS_KEY_SECONDARY_COLOR = "secondaryColor";
constexpr auto* SETTINGS_KEY_ERROR_COLOR = "errorColor";

static std::optional<uint32_t> parseColor(const std::map<std::string, std::string>& map, const char* key) {
    const auto entry = map.find(key);
    if (entry == map.end() || entry->second.empty()) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(std::strtoul(entry->second.c_str(), nullptr, 16)) & 0xFFFFFF;
}

static void putColor(std::map<std::string, std::string>& map, const char* key, std::optional<uint32_t> color) {
    if (color.has_value()) {
        char text[8];
        std::snprintf(text, sizeof(text), "%06" PRIX32, *color);
        map[key] = text;
    }
}

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
    if (const auto entry = map.find(SETTINGS_KEY_THEME_MODE); entry != map.end()) {
        if (entry->second == "light") {
            settings.themeMode = ThemeMode::Light;
        } else if (entry->second == "dark") {
            settings.themeMode = ThemeMode::Dark;
        }
    }
    if (const auto entry = map.find(SETTINGS_KEY_MONO_THEME); entry != map.end()) {
        settings.monoTheme = entry->second == "true";
    }
    if (const auto entry = map.find(SETTINGS_KEY_ANIMATIONS); entry != map.end()) {
        settings.animationsEnabled = entry->second != "false";
    }
    settings.primaryColor = parseColor(map, SETTINGS_KEY_PRIMARY_COLOR);
    settings.secondaryColor = parseColor(map, SETTINGS_KEY_SECONDARY_COLOR);
    settings.errorColor = parseColor(map, SETTINGS_KEY_ERROR_COLOR);
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
    if (settings.themeMode != ThemeMode::DeviceDefault) {
        map[SETTINGS_KEY_THEME_MODE] = settings.themeMode == ThemeMode::Dark ? "dark" : "light";
    }
    map[SETTINGS_KEY_MONO_THEME] = settings.monoTheme ? "true" : "false";
    map[SETTINGS_KEY_ANIMATIONS] = settings.animationsEnabled ? "true" : "false";
    putColor(map, SETTINGS_KEY_PRIMARY_COLOR, settings.primaryColor);
    putColor(map, SETTINGS_KEY_SECONDARY_COLOR, settings.secondaryColor);
    putColor(map, SETTINGS_KEY_ERROR_COLOR, settings.errorColor);
    return file::savePropertiesFile(settings_path, map);
}

}
