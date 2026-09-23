#include <Tactility/settings/TrackballSettings.h>

#include "tactility/paths.h"

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>

#include <app/paths.h>

#include <map>
#include <string>
#include <algorithm>

namespace tt::settings::trackball {

static std::string getSettingsFilePath() {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (app_paths_get_user_data_path("tactility.trackballsettings", "trackball.properties", path, sizeof(path)) != ERROR_NONE) {
        return "";
    }
    return path;
}

constexpr auto* KEY_TRACKBALL_ENABLED = "trackballEnabled";
constexpr auto* KEY_TRACKBALL_MODE = "trackballMode";
constexpr auto* KEY_ENCODER_SENSITIVITY = "encoderSensitivity";
constexpr auto* KEY_POINTER_SENSITIVITY = "pointerSensitivity";

constexpr uint8_t MIN_ENCODER_SENSITIVITY = 1;
constexpr uint8_t MAX_ENCODER_SENSITIVITY = 10;
constexpr uint8_t MIN_POINTER_SENSITIVITY = 1;
constexpr uint8_t MAX_POINTER_SENSITIVITY = 10;

bool load(LvglTrackballSettings& settings) {
    auto settings_path = getSettingsFilePath();
    if (!file::isFile(settings_path)) {
        return false;
    }

    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(settings_path, map)) {
        return false;
    }

    auto tb_enabled = map.find(KEY_TRACKBALL_ENABLED);
    auto tb_mode = map.find(KEY_TRACKBALL_MODE);
    auto enc_sens = map.find(KEY_ENCODER_SENSITIVITY);
    auto ptr_sens = map.find(KEY_POINTER_SENSITIVITY);

    // Safe integer parsing without exceptions
    auto safeParseUint8 = [](const std::string& str, uint8_t defaultVal) -> uint8_t {
        if (str.empty()) return defaultVal;
        unsigned int val = 0;
        for (char c : str) {
            if (c < '0' || c > '9') return defaultVal;
            if (val > 25) return defaultVal; // Early exit: val*10+9 would exceed 255
            val = val * 10 + (c - '0');
            if (val > 255) return defaultVal;
        }
        return static_cast<uint8_t>(val);
    };

    auto isTrueValue = [](const std::string& s) {
        return s == "1" || s == "true" || s == "True" || s == "TRUE";
    };
    settings.enabled = (tb_enabled != map.end()) ? isTrueValue(tb_enabled->second) : true;
    settings.mode = (tb_mode != map.end() && tb_mode->second == "1") ? LVGL_TRACKBALL_MODE_POINTER : LVGL_TRACKBALL_MODE_ENCODER;
    auto default_settings = lvgl_trackball_settings_get_default();
    settings.encoder_sensitivity = (enc_sens != map.end()) ? safeParseUint8(enc_sens->second, default_settings.encoder_sensitivity) : default_settings.encoder_sensitivity;
    settings.pointer_sensitivity = (ptr_sens != map.end()) ? safeParseUint8(ptr_sens->second, default_settings.pointer_sensitivity) : default_settings.pointer_sensitivity;

    // Clamp values to valid ranges
    settings.encoder_sensitivity = std::clamp(settings.encoder_sensitivity, MIN_ENCODER_SENSITIVITY, MAX_ENCODER_SENSITIVITY);
    settings.pointer_sensitivity = std::clamp(settings.pointer_sensitivity, MIN_POINTER_SENSITIVITY, MAX_POINTER_SENSITIVITY);

    return true;
}

LvglTrackballSettings getDefault() {
    return lvgl_trackball_settings_get_default();
}

LvglTrackballSettings loadOrGetDefault() {
    LvglTrackballSettings s;
    if (!load(s)) {
        s = getDefault();
    }
    return s;
}

bool save(const LvglTrackballSettings& settings) {
    std::map<std::string, std::string> map;
    map[KEY_TRACKBALL_ENABLED] = settings.enabled ? "1" : "0";
    map[KEY_TRACKBALL_MODE] = (settings.mode == LVGL_TRACKBALL_MODE_POINTER) ? "1" : "0";
    map[KEY_ENCODER_SENSITIVITY] = std::to_string(std::clamp(settings.encoder_sensitivity, MIN_ENCODER_SENSITIVITY, MAX_ENCODER_SENSITIVITY));
    map[KEY_POINTER_SENSITIVITY] = std::to_string(std::clamp(settings.pointer_sensitivity, MIN_POINTER_SENSITIVITY, MAX_POINTER_SENSITIVITY));
    auto settings_path = getSettingsFilePath();
    if (!file::findOrCreateParentDirectory(settings_path, 0755)) {
        return false;
    }
    return file::savePropertiesFile(settings_path, map);
}

}
