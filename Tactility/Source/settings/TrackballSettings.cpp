#include <Tactility/settings/TrackballSettings.h>
#include <Tactility/file/PropertiesFile.h>

#include <map>
#include <string>
#include <algorithm>

namespace tt::settings::trackball {

constexpr auto* SETTINGS_FILE = "/data/settings/trackball.properties";
constexpr auto* KEY_TRACKBALL_ENABLED = "trackballEnabled";
constexpr auto* KEY_TRACKBALL_MODE = "trackballMode";
constexpr auto* KEY_ENCODER_SENSITIVITY = "encoderSensitivity";
constexpr auto* KEY_POINTER_SENSITIVITY = "pointerSensitivity";

constexpr uint8_t MIN_ENCODER_SENSITIVITY = 1;
constexpr uint8_t MAX_ENCODER_SENSITIVITY = 10;
constexpr uint8_t MIN_POINTER_SENSITIVITY = 1;
constexpr uint8_t MAX_POINTER_SENSITIVITY = 10;

bool load(TrackballSettings& settings) {
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(SETTINGS_FILE, map)) {
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
    settings.trackballEnabled = (tb_enabled != map.end()) ? isTrueValue(tb_enabled->second) : true;
    settings.trackballMode = (tb_mode != map.end() && tb_mode->second == "1") ? TrackballMode::Pointer : TrackballMode::Encoder;
    settings.encoderSensitivity = (enc_sens != map.end()) ? safeParseUint8(enc_sens->second, MIN_ENCODER_SENSITIVITY) : MIN_ENCODER_SENSITIVITY;
    settings.pointerSensitivity = (ptr_sens != map.end()) ? safeParseUint8(ptr_sens->second, MAX_POINTER_SENSITIVITY) : MAX_POINTER_SENSITIVITY;

    // Clamp values to valid ranges
    settings.encoderSensitivity = std::clamp(settings.encoderSensitivity, MIN_ENCODER_SENSITIVITY, MAX_ENCODER_SENSITIVITY);
    settings.pointerSensitivity = std::clamp(settings.pointerSensitivity, MIN_POINTER_SENSITIVITY, MAX_POINTER_SENSITIVITY);

    return true;
}

TrackballSettings getDefault() {
    return TrackballSettings{
        .trackballEnabled = true,
        .trackballMode = TrackballMode::Encoder,
        .encoderSensitivity = MIN_ENCODER_SENSITIVITY,
        .pointerSensitivity = MAX_POINTER_SENSITIVITY
    };
}

TrackballSettings loadOrGetDefault() {
    TrackballSettings s;
    if (!load(s)) {
        s = getDefault();
    }
    return s;
}

bool save(const TrackballSettings& settings) {
    std::map<std::string, std::string> map;
    map[KEY_TRACKBALL_ENABLED] = settings.trackballEnabled ? "1" : "0";
    map[KEY_TRACKBALL_MODE] = (settings.trackballMode == TrackballMode::Pointer) ? "1" : "0";
    map[KEY_ENCODER_SENSITIVITY] = std::to_string(std::clamp(settings.encoderSensitivity, MIN_ENCODER_SENSITIVITY, MAX_ENCODER_SENSITIVITY));
    map[KEY_POINTER_SENSITIVITY] = std::to_string(std::clamp(settings.pointerSensitivity, MIN_POINTER_SENSITIVITY, MAX_POINTER_SENSITIVITY));
    return file::savePropertiesFile(SETTINGS_FILE, map);
}

}
