#include <Tactility/settings/TouchCalibrationSettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/Paths.h>

#include <cstdlib>
#include <cerrno>
#include <climits>
#include <map>
#include <string>

namespace tt::settings::touch {

static std::string getSettingsFilePath() {
    return getUserDataPath() + "/settings/touch-calibration.properties";
}

constexpr auto* SETTINGS_KEY_ENABLED = "enabled";
constexpr auto* SETTINGS_KEY_X_MIN = "xMin";
constexpr auto* SETTINGS_KEY_X_MAX = "xMax";
constexpr auto* SETTINGS_KEY_Y_MIN = "yMin";
constexpr auto* SETTINGS_KEY_Y_MAX = "yMax";

static bool toBool(const std::string& value) {
    return value == "1" || value == "true" || value == "True";
}

static bool parseInt32(const std::string& value, int32_t& out) {
    errno = 0;
    char* end_ptr = nullptr;
    const long parsed = std::strtol(value.c_str(), &end_ptr, 10);
    if (errno != 0 || end_ptr == value.c_str() || *end_ptr != '\0') {
        return false;
    }
    if (parsed < INT32_MIN || parsed > INT32_MAX) {
        return false;
    }
    out = static_cast<int32_t>(parsed);
    return true;
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
    auto settings_path = getSettingsFilePath();
    if (!file::isFile(settings_path)) {
        return false;
    }

    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(settings_path, map)) {
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
    if (!parseInt32(x_min_it->second, loaded.xMin) ||
        !parseInt32(x_max_it->second, loaded.xMax) ||
        !parseInt32(y_min_it->second, loaded.yMin) ||
        !parseInt32(y_max_it->second, loaded.yMax)) {
        return false;
    }

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
    if (settings.enabled && !isValid(settings)) {
        return false;
    }

    std::map<std::string, std::string> map;
    map[SETTINGS_KEY_ENABLED] = settings.enabled ? "1" : "0";
    map[SETTINGS_KEY_X_MIN] = std::to_string(settings.xMin);
    map[SETTINGS_KEY_X_MAX] = std::to_string(settings.xMax);
    map[SETTINGS_KEY_Y_MIN] = std::to_string(settings.yMin);
    map[SETTINGS_KEY_Y_MAX] = std::to_string(settings.yMax);

    auto settings_path = getSettingsFilePath();
    if (!file::findOrCreateParentDirectory(settings_path, 0755)) {
        return false;
    }

    return file::savePropertiesFile(settings_path, map);
}

} // namespace tt::settings::touch
