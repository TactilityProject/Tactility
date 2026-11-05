#include <Tactility/settings/GrovePortSettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>

#include <tactility/device.h>
#include <tactility/log.h>
#include <tactility/paths.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace tt::settings::grove {

constexpr auto* TAG = "GroveSettings";

static bool getSettingsFilePath(std::string& outPath) {
    char root[128];
    if (paths_get_data_path(root, sizeof(root)) != ERROR_NONE) {
        return false;
    }
    outPath = std::string(root) + "/settings/grove.properties";
    return true;
}

static const char* toString(GroveMode mode) {
    switch (mode) {
        case GROVE_MODE_UART: return "uart";
        case GROVE_MODE_I2C: return "i2c";
        default: return "disabled";
    }
}

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

static bool loadMap(std::map<std::string, std::string>& map) {
    std::string path;
    if (!getSettingsFilePath(path) || !file::isFile(path)) {
        return false;
    }
    return file::loadPropertiesFile(path, map);
}

bool loadMode(const char* deviceName, GroveMode& mode) {
    std::map<std::string, std::string> map;
    if (!loadMap(map)) {
        return false;
    }
    auto entry = map.find(deviceName);
    return entry != map.end() && fromString(entry->second, mode);
}

bool saveMode(const char* deviceName, GroveMode mode) {
    std::map<std::string, std::string> map;
    loadMap(map);
    map[deviceName] = toString(mode);

    std::string path;
    if (!getSettingsFilePath(path)) {
        return false;
    }
    if (!file::findOrCreateParentDirectory(path, 0755)) {
        return false;
    }
    return file::savePropertiesFile(path, map);
}

// Modes are set after the iteration, because changing a mode adds and removes child devices
void restoreAll() {
    std::map<std::string, std::string> map;
    if (!loadMap(map)) {
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
