#include <Tactility/settings/AppearanceSettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>

#include <app/paths.h>

#include <charconv>
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
constexpr auto* SETTINGS_KEY_DENSITY = "density";
constexpr auto* SETTINGS_KEY_ANIMATIONS = "animations";
constexpr auto* SETTINGS_KEY_PRIMARY_COLOR = "primaryColor";
constexpr auto* SETTINGS_KEY_SECONDARY_COLOR = "secondaryColor";
constexpr auto* SETTINGS_KEY_ERROR_COLOR = "errorColor";
constexpr auto* SETTINGS_KEY_SURFACE_COLOR = "surfaceColor";
constexpr auto* SETTINGS_KEY_SURFACE_TINT_LEVEL = "surfaceTintLevel";

// The share of the surface colour per tint level, in percent
constexpr uint32_t SURFACE_TINT_PERCENTAGES[SURFACE_TINT_LEVEL_COUNT] = { 15, 25, 40 };

static uint32_t mixColor(uint32_t color, uint32_t base, uint32_t colorPercentage) {
    uint32_t result = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        const uint32_t channel = (((color >> shift) & 0xFF) * colorPercentage + ((base >> shift) & 0xFF) * (100 - colorPercentage) + 50) / 100;
        result |= channel << shift;
    }
    return result;
}

uint32_t getSurfaceColor(uint32_t color, uint8_t tintLevel, bool isDark) {
    const uint32_t base = isDark ? 0x000000 : 0xFFFFFF;
    const uint8_t level = tintLevel < SURFACE_TINT_LEVEL_COUNT ? tintLevel : SURFACE_TINT_LEVEL_COUNT - 1;
    return mixColor(color, base, SURFACE_TINT_PERCENTAGES[level]);
}

static std::optional<uint32_t> parseColor(const std::map<std::string, std::string>& map, const char* key) {
    const auto entry = map.find(key);
    if (entry == map.end() || entry->second.empty()) {
        return std::nullopt;
    }
    // An invalid value (e.g. from a hand-edited file) counts as unset, rather than as black
    const std::string& text = entry->second;
    uint32_t color = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), color, 16);
    if (error != std::errc {} || end != text.data() + text.size() || color > 0xFFFFFF) {
        return std::nullopt;
    }
    return color;
}

// Saving only updates the keys that are written, so an unset colour is written as an empty value
static void putColor(std::map<std::string, std::string>& map, const char* key, std::optional<uint32_t> color) {
    if (color.has_value()) {
        char text[8];
        std::snprintf(text, sizeof(text), "%06" PRIX32, *color);
        map[key] = text;
    } else {
        map[key] = "";
    }
}

static const char* toString(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Light:
            return "light";
        case ThemeMode::Dark:
            return "dark";
        default:
            return "default";
    }
}

static const char* toString(DensityMode mode) {
    switch (mode) {
        case DensityMode::Regular:
            return "regular";
        case DensityMode::Compact:
            return "compact";
        default:
            return "default";
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
    if (const auto entry = map.find(SETTINGS_KEY_DENSITY); entry != map.end()) {
        if (entry->second == "regular") {
            settings.densityMode = DensityMode::Regular;
        } else if (entry->second == "compact") {
            settings.densityMode = DensityMode::Compact;
        }
    }
    if (const auto entry = map.find(SETTINGS_KEY_ANIMATIONS); entry != map.end()) {
        settings.animationsEnabled = entry->second != "false";
    }
    settings.primaryColor = parseColor(map, SETTINGS_KEY_PRIMARY_COLOR);
    settings.secondaryColor = parseColor(map, SETTINGS_KEY_SECONDARY_COLOR);
    settings.errorColor = parseColor(map, SETTINGS_KEY_ERROR_COLOR);
    settings.surfaceColor = parseColor(map, SETTINGS_KEY_SURFACE_COLOR);
    if (const auto entry = map.find(SETTINGS_KEY_SURFACE_TINT_LEVEL); entry != map.end()) {
        const std::string& text = entry->second;
        uint32_t level = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), level);
        if (error == std::errc {} && end == text.data() + text.size() && level < SURFACE_TINT_LEVEL_COUNT) {
            settings.surfaceTintLevel = static_cast<uint8_t>(level);
        }
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
    map[SETTINGS_KEY_THEME_MODE] = toString(settings.themeMode);
    map[SETTINGS_KEY_MONO_THEME] = settings.monoTheme ? "true" : "false";
    map[SETTINGS_KEY_DENSITY] = toString(settings.densityMode);
    map[SETTINGS_KEY_ANIMATIONS] = settings.animationsEnabled ? "true" : "false";
    putColor(map, SETTINGS_KEY_PRIMARY_COLOR, settings.primaryColor);
    putColor(map, SETTINGS_KEY_SECONDARY_COLOR, settings.secondaryColor);
    putColor(map, SETTINGS_KEY_ERROR_COLOR, settings.errorColor);
    putColor(map, SETTINGS_KEY_SURFACE_COLOR, settings.surfaceColor);
    map[SETTINGS_KEY_SURFACE_TINT_LEVEL] = std::to_string(settings.surfaceTintLevel);
    return file::savePropertiesFile(settings_path, map);
}

}
