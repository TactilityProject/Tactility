#include <Tactility/settings/TouchCalibrationSettings.h>

#include <Tactility/file/PropertiesFile.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>

namespace tt::settings::touch {

constexpr auto* SETTINGS_FILE = "/data/settings/touch-calibration.properties";
constexpr auto* SETTINGS_KEY_ENABLED = "enabled";
constexpr auto* SETTINGS_KEY_X_MIN = "xMin";
constexpr auto* SETTINGS_KEY_X_MAX = "xMax";
constexpr auto* SETTINGS_KEY_Y_MIN = "yMin";
constexpr auto* SETTINGS_KEY_Y_MAX = "yMax";

static bool cacheInitialized = false;
static TouchCalibrationSettings cachedSettings;

static bool toBool(const std::string& value) {
    return value == "1" || value == "true" || value == "True";
}

TouchCalibrationSettings getDefault() {
    return {
        .enabled = false,
        .xMin = 0,
        .xMax = 0,
        .yMin = 0,
        .yMax = 0,
    };
}

bool isValid(const TouchCalibrationSettings& settings) {
    constexpr auto MIN_RANGE = 20;
    return settings.xMax > settings.xMin && settings.yMax > settings.yMin &&
        (settings.xMax - settings.xMin) >= MIN_RANGE &&
        (settings.yMax - settings.yMin) >= MIN_RANGE;
}

bool load(TouchCalibrationSettings& settings) {
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(SETTINGS_FILE, map)) {
        return false;
    }

    auto enabled_it = map.find(SETTINGS_KEY_ENABLED);
    auto x_min_it = map.find(SETTINGS_KEY_X_MIN);
    auto x_max_it = map.find(SETTINGS_KEY_X_MAX);
    auto y_min_it = map.find(SETTINGS_KEY_Y_MIN);
    auto y_max_it = map.find(SETTINGS_KEY_Y_MAX);

    if (enabled_it == map.end() || x_min_it == map.end() || x_max_it == map.end() || y_min_it == map.end() || y_max_it == map.end()) {
        return false;
    }

    TouchCalibrationSettings loaded = getDefault();
    loaded.enabled = toBool(enabled_it->second);
    loaded.xMin = static_cast<int32_t>(std::strtol(x_min_it->second.c_str(), nullptr, 10));
    loaded.xMax = static_cast<int32_t>(std::strtol(x_max_it->second.c_str(), nullptr, 10));
    loaded.yMin = static_cast<int32_t>(std::strtol(y_min_it->second.c_str(), nullptr, 10));
    loaded.yMax = static_cast<int32_t>(std::strtol(y_max_it->second.c_str(), nullptr, 10));

    if (loaded.enabled && !isValid(loaded)) {
        return false;
    }

    settings = loaded;
    return true;
}

TouchCalibrationSettings loadOrGetDefault() {
    TouchCalibrationSettings settings;
    if (!load(settings)) {
        settings = getDefault();
    }
    return settings;
}

bool save(const TouchCalibrationSettings& settings) {
    std::map<std::string, std::string> map;
    map[SETTINGS_KEY_ENABLED] = settings.enabled ? "1" : "0";
    map[SETTINGS_KEY_X_MIN] = std::to_string(settings.xMin);
    map[SETTINGS_KEY_X_MAX] = std::to_string(settings.xMax);
    map[SETTINGS_KEY_Y_MIN] = std::to_string(settings.yMin);
    map[SETTINGS_KEY_Y_MAX] = std::to_string(settings.yMax);

    if (!file::savePropertiesFile(SETTINGS_FILE, map)) {
        return false;
    }

    cachedSettings = settings;
    cacheInitialized = true;
    return true;
}

TouchCalibrationSettings getActive() {
    if (!cacheInitialized) {
        cachedSettings = loadOrGetDefault();
        cacheInitialized = true;
    }
    return cachedSettings;
}

bool applyCalibration(const TouchCalibrationSettings& settings, uint16_t xMax, uint16_t yMax, uint16_t& x, uint16_t& y) {
    if (!settings.enabled || !isValid(settings)) {
        return false;
    }

    const int32_t in_x = static_cast<int32_t>(x);
    const int32_t in_y = static_cast<int32_t>(y);

    const int32_t mapped_x = (in_x - settings.xMin) * static_cast<int32_t>(xMax) / (settings.xMax - settings.xMin);
    const int32_t mapped_y = (in_y - settings.yMin) * static_cast<int32_t>(yMax) / (settings.yMax - settings.yMin);

    x = static_cast<uint16_t>(std::clamp(mapped_x, 0, static_cast<int32_t>(xMax)));
    y = static_cast<uint16_t>(std::clamp(mapped_y, 0, static_cast<int32_t>(yMax)));
    return true;
}

} // namespace tt::settings::touch
