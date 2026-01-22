#include <Tactility/settings/KeyboardSettings.h>
#include <Tactility/file/PropertiesFile.h>

#include <map>
#include <string>

namespace tt::settings::keyboard {

constexpr auto* SETTINGS_FILE = "/data/settings/keyboard.properties";
constexpr auto* KEY_BACKLIGHT_ENABLED = "backlightEnabled";
constexpr auto* KEY_BACKLIGHT_BRIGHTNESS = "backlightBrightness";
constexpr auto* KEY_BACKLIGHT_TIMEOUT_ENABLED = "backlightTimeoutEnabled";
constexpr auto* KEY_BACKLIGHT_TIMEOUT_MS = "backlightTimeoutMs";

bool load(KeyboardSettings& settings) {
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(SETTINGS_FILE, map)) {
        return false;
    }

    auto bl_enabled = map.find(KEY_BACKLIGHT_ENABLED);
    auto bl_brightness = map.find(KEY_BACKLIGHT_BRIGHTNESS);
    auto bl_timeout_enabled = map.find(KEY_BACKLIGHT_TIMEOUT_ENABLED);
    auto bl_timeout_ms = map.find(KEY_BACKLIGHT_TIMEOUT_MS);

    settings.backlightEnabled = (bl_enabled != map.end()) ? (bl_enabled->second == "1" || bl_enabled->second == "true" || bl_enabled->second == "True") : true;
    settings.backlightBrightness = (bl_brightness != map.end()) ? static_cast<uint8_t>(std::stoi(bl_brightness->second)) : 127;
    settings.backlightTimeoutEnabled = (bl_timeout_enabled != map.end()) ? (bl_timeout_enabled->second == "1" || bl_timeout_enabled->second == "true" || bl_timeout_enabled->second == "True") : true;
    settings.backlightTimeoutMs = (bl_timeout_ms != map.end()) ? static_cast<uint32_t>(std::stoul(bl_timeout_ms->second)) : 60000; // Default 60 seconds

    return true;
}

KeyboardSettings getDefault() {
    return KeyboardSettings{
        .backlightEnabled = true,
        .backlightBrightness = 127,
        .backlightTimeoutEnabled = true,
        .backlightTimeoutMs = 60000 // 60 seconds default
    };
}

KeyboardSettings loadOrGetDefault() {
    KeyboardSettings s;
    if (!load(s)) {
        s = getDefault();
    }
    return s;
}

bool save(const KeyboardSettings& settings) {
    std::map<std::string, std::string> map;
    map[KEY_BACKLIGHT_ENABLED] = settings.backlightEnabled ? "1" : "0";
    map[KEY_BACKLIGHT_BRIGHTNESS] = std::to_string(settings.backlightBrightness);
    map[KEY_BACKLIGHT_TIMEOUT_ENABLED] = settings.backlightTimeoutEnabled ? "1" : "0";
    map[KEY_BACKLIGHT_TIMEOUT_MS] = std::to_string(settings.backlightTimeoutMs);
    return file::savePropertiesFile(SETTINGS_FILE, map);
}

}
