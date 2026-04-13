#include <Tactility/bluetooth/BluetoothSettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/Logger.h>
#include <Tactility/Mutex.h>

namespace tt::bluetooth::settings {

static const auto LOGGER = Logger("BluetoothSettings");

// Use the same path as the old service so existing settings survive migration.
constexpr auto* SETTINGS_PATH         = "/data/service/bluetooth/settings.properties";
constexpr auto* KEY_ENABLE_ON_BOOT    = "enableOnBoot";
constexpr auto* KEY_SPP_AUTO_START    = "sppAutoStart";
constexpr auto* KEY_MIDI_AUTO_START   = "midiAutoStart";

struct BluetoothSettings {
    bool enableOnBoot  = false;
    bool sppAutoStart  = false;
    bool midiAutoStart = false;
};

static Mutex settings_mutex;
static BluetoothSettings cached;
static bool cached_valid = false;

static bool load(BluetoothSettings& out) {
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(SETTINGS_PATH, map)) {
        return false;
    }
    auto it = map.find(KEY_ENABLE_ON_BOOT);
    if (it == map.end()) return false;
    out.enableOnBoot = (it->second == "true");

    auto spp_it = map.find(KEY_SPP_AUTO_START);
    out.sppAutoStart = (spp_it != map.end() && spp_it->second == "true");

    auto midi_it = map.find(KEY_MIDI_AUTO_START);
    out.midiAutoStart = (midi_it != map.end() && midi_it->second == "true");
    return true;
}

static bool save(const BluetoothSettings& s) {
    std::map<std::string, std::string> map;
    file::loadPropertiesFile(SETTINGS_PATH, map); // ignore failure — may not exist yet
    map[KEY_ENABLE_ON_BOOT]  = s.enableOnBoot  ? "true" : "false";
    map[KEY_SPP_AUTO_START]  = s.sppAutoStart  ? "true" : "false";
    map[KEY_MIDI_AUTO_START] = s.midiAutoStart ? "true" : "false";
    return file::savePropertiesFile(SETTINGS_PATH, map);
}

static BluetoothSettings getCachedOrLoad() {
    settings_mutex.lock();
    if (!cached_valid) {
        if (!load(cached)) {
            cached = BluetoothSettings{};
        }
        cached_valid = true;
    }
    auto result = cached;
    settings_mutex.unlock();
    return result;
}

void setEnableOnBoot(bool enable) {
    settings_mutex.lock();
    cached.enableOnBoot = enable;
    cached_valid = true;
    settings_mutex.unlock();
    if (!save(cached)) LOGGER.error("Failed to save");
}

bool shouldEnableOnBoot() {
    return getCachedOrLoad().enableOnBoot;
}

void setSppAutoStart(bool enable) {
    settings_mutex.lock();
    cached.sppAutoStart = enable;
    cached_valid = true;
    settings_mutex.unlock();
    if (!save(cached)) LOGGER.error("Failed to save (setSppAutoStart)");
}

bool shouldSppAutoStart() {
    return getCachedOrLoad().sppAutoStart;
}

void setMidiAutoStart(bool enable) {
    settings_mutex.lock();
    cached.midiAutoStart = enable;
    cached_valid = true;
    settings_mutex.unlock();
    if (!save(cached)) LOGGER.error("Failed to save (setMidiAutoStart)");
}

bool shouldMidiAutoStart() {
    return getCachedOrLoad().midiAutoStart;
}

} // namespace tt::bluetooth::settings
