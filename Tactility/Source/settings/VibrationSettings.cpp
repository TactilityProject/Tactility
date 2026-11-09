// SPDX-License-Identifier: Apache-2.0
#include <Tactility/settings/VibrationSettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>

#include <app/paths.h>

#include <cstdlib>
#include <map>
#include <string>

namespace tt::settings::vibration {

namespace {

constexpr auto* SETTINGS_KEY_ENABLED = "enabled";
constexpr auto* SETTINGS_KEY_STRENGTH = "strength";

std::string getSettingsFilePath() {
    char path[256];
    if (app_paths_get_user_data_path("tactility.vibrationsettings", "vibration.properties", path, sizeof(path)) != ERROR_NONE) {
        return "";
    }
    return path;
}

bool parseBool(const std::string& value) {
    return value == "1" || value == "true" || value == "True";
}

} // namespace

bool load(VibrationSettings& settings) {
    auto settings_path = getSettingsFilePath();
    if (settings_path.empty() || !file::isFile(settings_path)) {
        return false;
    }

    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(settings_path, map)) {
        return false;
    }

    bool enabled = true;
    auto enabled_entry = map.find(SETTINGS_KEY_ENABLED);
    if (enabled_entry != map.end()) {
        enabled = parseBool(enabled_entry->second);
    }

    int strength = 80;
    auto strength_entry = map.find(SETTINGS_KEY_STRENGTH);
    if (strength_entry != map.end()) {
        strength = atoi(strength_entry->second.c_str());
        if (strength < 0) strength = 0;
        if (strength > 100) strength = 100;
    }

    settings.enabled = enabled;
    settings.strength = static_cast<uint8_t>(strength);
    return true;
}

VibrationSettings getDefault() {
    return VibrationSettings {
        .enabled = true,
        .strength = 80,
    };
}

VibrationSettings loadOrGetDefault() {
    VibrationSettings settings;
    if (!load(settings)) {
        settings = getDefault();
    }
    return settings;
}

bool save(const VibrationSettings& settings) {
    std::map<std::string, std::string> map;
    map[SETTINGS_KEY_ENABLED] = settings.enabled ? "1" : "0";
    map[SETTINGS_KEY_STRENGTH] = std::to_string(settings.strength);

    auto settings_path = getSettingsFilePath();
    if (settings_path.empty()) {
        return false;
    }
    if (!file::findOrCreateParentDirectory(settings_path, 0755)) {
        return false;
    }
    return file::savePropertiesFile(settings_path, map);
}

} // namespace tt::settings::vibration
