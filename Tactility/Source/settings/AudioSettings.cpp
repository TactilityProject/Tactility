#include <Tactility/settings/AudioSettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/Paths.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace tt::settings::audio {

static std::string getSettingsFilePath() {
    return getUserDataPath() + "/settings/audio.properties";
}

constexpr auto* SETTINGS_KEY_INPUT_ENABLED = "inputEnabled";
constexpr auto* SETTINGS_KEY_OUTPUT_ENABLED = "outputEnabled";
constexpr auto* SETTINGS_KEY_INPUT_MUTED = "inputMuted";
constexpr auto* SETTINGS_KEY_OUTPUT_MUTED = "outputMuted";
constexpr auto* SETTINGS_KEY_INPUT_VOLUME = "inputVolume";
constexpr auto* SETTINGS_KEY_OUTPUT_VOLUME = "outputVolume";

static bool toBool(const std::map<std::string, std::string>& map, const char* key, bool defaultValue) {
    auto entry = map.find(key);
    if (entry == map.end()) {
        return defaultValue;
    }
    return (entry->second == "1" || entry->second == "true" || entry->second == "True");
}

static float toFloat(const std::map<std::string, std::string>& map, const char* key, float defaultValue) {
    auto entry = map.find(key);
    if (entry == map.end()) {
        return defaultValue;
    }
    // Volume is documented/consumed as a 0..100 percentage (audio_stream_set_volume); clamp
    // here so a hand-edited or corrupted properties file can't push out-of-range values
    // through to the codec.
    float value = std::strtof(entry->second.c_str(), nullptr);
    return std::clamp(value, 0.0f, 100.0f);
}

static std::string toString(bool value) {
    return value ? "1" : "0";
}

static std::string toString(float value) {
    // std::to_string always emits 6 decimals (e.g. "20.000000"); volume is a 0..100
    // percentage where whole-number precision is all that's meaningful here.
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.1f", (double) value);
    return std::string(buffer);
}

bool load(AudioSettings& settings) {
    auto settings_path = getSettingsFilePath();
    if (!file::isFile(settings_path)) {
        return false;
    }

    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(settings_path, map)) {
        return false;
    }

    settings.inputEnabled = toBool(map, SETTINGS_KEY_INPUT_ENABLED, true);
    settings.outputEnabled = toBool(map, SETTINGS_KEY_OUTPUT_ENABLED, true);
    settings.inputMuted = toBool(map, SETTINGS_KEY_INPUT_MUTED, false);
    settings.outputMuted = toBool(map, SETTINGS_KEY_OUTPUT_MUTED, false);
    settings.inputVolume = toFloat(map, SETTINGS_KEY_INPUT_VOLUME, 90.0f);
    settings.outputVolume = toFloat(map, SETTINGS_KEY_OUTPUT_VOLUME, 50.0f);

    return true;
}

AudioSettings getDefault() {
    return AudioSettings {
        .inputEnabled = true,
        .outputEnabled = true,
        .inputMuted = false,
        .outputMuted = false,
        .inputVolume = 90.0f,
        .outputVolume = 50.0f
    };
}

AudioSettings loadOrGetDefault() {
    AudioSettings settings;
    if (!load(settings)) {
        settings = getDefault();
    }
    return settings;
}

bool save(const AudioSettings& settings) {
    std::map<std::string, std::string> map;
    map[SETTINGS_KEY_INPUT_ENABLED] = toString(settings.inputEnabled);
    map[SETTINGS_KEY_OUTPUT_ENABLED] = toString(settings.outputEnabled);
    map[SETTINGS_KEY_INPUT_MUTED] = toString(settings.inputMuted);
    map[SETTINGS_KEY_OUTPUT_MUTED] = toString(settings.outputMuted);
    map[SETTINGS_KEY_INPUT_VOLUME] = toString(settings.inputVolume);
    map[SETTINGS_KEY_OUTPUT_VOLUME] = toString(settings.outputVolume);
    auto settings_path = getSettingsFilePath();
    if (!file::findOrCreateParentDirectory(settings_path, 0755)) {
        return false;
    }
    return file::savePropertiesFile(settings_path, map);
}

} // namespace tt::settings::audio
