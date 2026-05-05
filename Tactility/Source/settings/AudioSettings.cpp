#include <Tactility/settings/AudioSettings.h>

#include <Tactility/file/PropertiesFile.h>

#include <map>
#include <string>

namespace tt::settings::audio {

constexpr auto* SETTINGS_FILE = "/data/settings/audio.properties";
constexpr auto* SETTINGS_KEY_VOLUME = "volume";
constexpr auto* SETTINGS_KEY_MUTED = "muted";

bool load(AudioSettings& settings) {
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(SETTINGS_FILE, map)) {
        return false;
    }

    auto volume_entry = map.find(SETTINGS_KEY_VOLUME);
    int volume = 70;
    if (volume_entry != map.end()) {
        volume = atoi(volume_entry->second.c_str());
        if (volume < 0) volume = 0;
        if (volume > 100) volume = 100;
    }

    bool muted = false;
    auto muted_entry = map.find(SETTINGS_KEY_MUTED);
    if (muted_entry != map.end()) {
        muted = (muted_entry->second == "1" || muted_entry->second == "true" || muted_entry->second == "True");
    }

    settings.volume = static_cast<uint8_t>(volume);
    settings.muted = muted;

    return true;
}

AudioSettings getDefault() {
    return AudioSettings {
        .volume = 70,
        .muted = false
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
    map[SETTINGS_KEY_VOLUME] = std::to_string(settings.volume);
    map[SETTINGS_KEY_MUTED] = settings.muted ? "1" : "0";
    return file::savePropertiesFile(SETTINGS_FILE, map);
}

} // namespace
