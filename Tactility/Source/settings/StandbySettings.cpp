#include <Tactility/settings/StandbySettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>

#include <tactility/filesystem/fs.h>
#include <tactility/paths.h>

#include <cstring>

namespace tt::settings::standby {

constexpr auto* KEY_KEEP_DISPLAY = "keepDisplay";
constexpr auto* KEY_KEEP_WIFI = "keepWifi";
constexpr auto* KEY_KEEP_BLUETOOTH = "keepBluetooth";
constexpr auto* KEY_KEEP_MESH = "keepMesh";
constexpr auto* KEY_WAKE_PREFIX = "wake.";

static bool getSettingsFilePath(::std::string& outPath) {
    char root[128];
    if (paths_get_data_path(root, sizeof(root)) != ERROR_NONE) {
        return false;
    }
    outPath = ::std::string(root) + "/settings/standby.properties";
    return true;
}

static bool toBool(const ::std::map<::std::string, ::std::string>& map, const char* key, bool defaultValue) {
    auto entry = map.find(key);
    if (entry == map.end()) {
        return defaultValue;
    }
    return entry->second == "1" || entry->second == "true" || entry->second == "True";
}

static const char* toString(bool value) {
    return value ? "1" : "0";
}

bool isWakeDefault(WakeKind kind) {
    return kind != WakeKind::Button;
}

bool isWakeEnabled(const StandbySettings& settings, const ::std::string& deviceName, WakeKind kind) {
    auto entry = settings.wake.find(deviceName);
    return entry != settings.wake.end() ? entry->second : isWakeDefault(kind);
}

StandbySettings getDefault() {
    return StandbySettings {
        .keepDisplay = false,
        .keepWifi = false,
        .keepBluetooth = false,
        .keepMesh = true,
        .wake = {}
    };
}

bool load(StandbySettings& settings) {
    ::std::string path;
    if (!getSettingsFilePath(path) || !file_exists(path.c_str())) {
        return false;
    }

    ::std::map<::std::string, ::std::string> map;
    if (!file::loadPropertiesFile(path, map)) {
        return false;
    }

    settings = getDefault();
    settings.keepDisplay = toBool(map, KEY_KEEP_DISPLAY, settings.keepDisplay);
    settings.keepWifi = toBool(map, KEY_KEEP_WIFI, settings.keepWifi);
    settings.keepBluetooth = toBool(map, KEY_KEEP_BLUETOOTH, settings.keepBluetooth);
    settings.keepMesh = toBool(map, KEY_KEEP_MESH, settings.keepMesh);
    const size_t prefix_length = strlen(KEY_WAKE_PREFIX);
    for (const auto& [key, value] : map) {
        if (key.compare(0, prefix_length, KEY_WAKE_PREFIX) == 0 && key.size() > prefix_length) {
            settings.wake[key.substr(prefix_length)] = (value == "1" || value == "true" || value == "True");
        }
    }
    return true;
}

StandbySettings loadOrGetDefault() {
    StandbySettings settings;
    if (!load(settings)) {
        settings = getDefault();
    }
    return settings;
}

bool save(const StandbySettings& settings) {
    ::std::map<::std::string, ::std::string> map;
    map[KEY_KEEP_DISPLAY] = toString(settings.keepDisplay);
    map[KEY_KEEP_WIFI] = toString(settings.keepWifi);
    map[KEY_KEEP_BLUETOOTH] = toString(settings.keepBluetooth);
    map[KEY_KEEP_MESH] = toString(settings.keepMesh);
    for (const auto& [name, enabled] : settings.wake) {
        map[KEY_WAKE_PREFIX + name] = toString(enabled);
    }

    ::std::string path;
    if (!getSettingsFilePath(path) || !file::findOrCreateParentDirectory(path, 0755)) {
        return false;
    }
    return file::savePropertiesFile(path, map);
}

} // namespace tt::settings::standby
