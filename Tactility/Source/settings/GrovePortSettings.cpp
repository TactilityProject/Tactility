#include <Tactility/settings/GrovePortSettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/log.h>
#include <tactility/paths.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace tt::settings::grove {

constexpr auto* TAG = "GroveSettings";

struct FileMutex {
    Mutex mutex {};
    FileMutex() { mutex_construct(&mutex); }
    ~FileMutex() { mutex_destruct(&mutex); }
};

// Saves read, change and write the whole file, so concurrent saves could overwrite each other's modes
static FileMutex file_mutex;

/** Gets the path of grove.properties in the user data folder. @return false when the data path is unknown */
static bool getSettingsFilePath(std::string& outPath) {
    char root[128];
    if (paths_get_data_path(root, sizeof(root)) != ERROR_NONE) {
        return false;
    }
    outPath = std::string(root) + "/settings/grove.properties";
    return true;
}

/** @return the value that grove.properties stores for the mode */
static const char* toString(GroveMode mode) {
    switch (mode) {
        case GROVE_MODE_UART: return "uart";
        case GROVE_MODE_I2C: return "i2c";
        default: return "disabled";
    }
}

/** Parses a mode stored in grove.properties. @return false for an unknown value */
static bool fromString(const std::string& text, GroveMode& mode) {
    if (text == "uart") {
        mode = GROVE_MODE_UART;
    } else if (text == "i2c") {
        mode = GROVE_MODE_I2C;
    } else if (text == "disabled") {
        mode = GROVE_MODE_DISABLED;
    } else {
        return false;
    }
    return true;
}

/** Loads grove.properties. Must be called with file_mutex held. @return false when the file is missing or unreadable */
static bool loadMap(std::map<std::string, std::string>& map) {
    std::string path;
    if (!getSettingsFilePath(path) || !file::isFile(path)) {
        return false;
    }
    return file::loadPropertiesFile(path, map);
}

bool loadMode(const char* deviceName, GroveMode& mode) {
    std::map<std::string, std::string> map;
    mutex_lock(&file_mutex.mutex);
    const bool loaded = loadMap(map);
    mutex_unlock(&file_mutex.mutex);
    if (!loaded) {
        return false;
    }
    auto entry = map.find(deviceName);
    return entry != map.end() && fromString(entry->second, mode);
}

bool saveMode(const char* deviceName, GroveMode mode) {
    std::string path;
    if (!getSettingsFilePath(path)) {
        return false;
    }

    mutex_lock(&file_mutex.mutex);
    std::map<std::string, std::string> map;
    loadMap(map);
    map[deviceName] = toString(mode);
    const bool saved = file::findOrCreateParentDirectory(path, 0755) && file::savePropertiesFile(path, map);
    mutex_unlock(&file_mutex.mutex);
    if (!saved) {
        LOG_E(TAG, "Failed to save the mode of %s", deviceName);
    }
    return saved;
}

// Modes are set after the iteration, because changing a mode adds and removes child devices
void restoreAll() {
    std::map<std::string, std::string> map;
    mutex_lock(&file_mutex.mutex);
    const bool loaded = loadMap(map);
    mutex_unlock(&file_mutex.mutex);
    if (!loaded) {
        return;
    }
    std::vector<Device*> devices;
    device_for_each_of_type(&GROVE_TYPE, &devices, [](Device* device, void* context) {
        static_cast<std::vector<Device*>*>(context)->push_back(device);
        return true;
    });
    for (auto* device : devices) {
        auto entry = map.find(device->name);
        GroveMode mode;
        if (entry == map.end() || !fromString(entry->second, mode)) {
            continue;
        }
        GroveMode current;
        if (grove_get_mode(device, &current) == ERROR_NONE && current == mode) {
            continue;
        }
        if (grove_set_mode(device, mode) != ERROR_NONE) {
            LOG_W(TAG, "Failed to restore %s to %s", device->name, entry->second.c_str());
        }
    }
}

} // namespace tt::settings::grove
