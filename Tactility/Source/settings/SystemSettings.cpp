#include <Tactility/Logger.h>
#include <Tactility/MountPoints.h>
#include <Tactility/Mutex.h>
#include <Tactility/file/File.h>
#include <Tactility/file/FileLock.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/settings/Language.h>
#include <Tactility/settings/SystemSettings.h>

#include "Tactility/Paths.h"

#include <format>

namespace tt::settings {

static const auto LOGGER = Logger("SystemSettings");

constexpr auto* FILE_PATH_FORMAT = "{}/settings/system.properties";

static bool cached = false;
static SystemSettings cachedSettings;

static bool hasSystemSettingsFile() {
    auto file_path = std::format(FILE_PATH_FORMAT, getUserDataPath());
    return file::isFile(file_path);
}

static bool loadSystemSettingsFromFile(SystemSettings& properties) {
    auto file_path = std::format(FILE_PATH_FORMAT, getUserDataPath());
    LOGGER.info("System settings loading from {}", file_path);
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(file_path, map)) {
        return false;
    }

    auto language_entry = map.find("language");
    if (language_entry != map.end()) {
        if (!fromString(language_entry->second, properties.language)) {
            LOGGER.warn("Unknown language \"{}\" in {}", language_entry->second, file_path);
            properties.language = Language::en_US;
        }
    } else {
        properties.language = Language::en_US;
    }

    auto time_format_entry = map.find("timeFormat24h");
    bool time_format_24h = time_format_entry == map.end() ? true : (time_format_entry->second == "true");
    properties.timeFormat24h = time_format_24h;

    // Load date format
    // Default to MM/DD/YYYY if missing (backward compat with older system.properties)
    auto date_format_entry = map.find("dateFormat");
    if (date_format_entry != map.end() && !date_format_entry->second.empty()) {
        properties.dateFormat = date_format_entry->second;
    } else {
        LOGGER.info("dateFormat missing or empty, using default MM/DD/YYYY (likely from older system.properties)");
        properties.dateFormat = "MM/DD/YYYY";
    }

    LOGGER.info("System settings loaded");
    return true;
}

bool loadSystemSettings(SystemSettings& properties) {
    if (!cached && hasSystemSettingsFile()) {
        if (loadSystemSettingsFromFile(cachedSettings)) {
            cached = true;
        } else {
            LOGGER.error("Failed to load");
        }
    }

    properties = cachedSettings;
    return true;
}

bool saveSystemSettings(const SystemSettings& properties) {
    auto file_path = std::format(FILE_PATH_FORMAT, file::MOUNT_POINT_DATA);
    std::map<std::string, std::string> map;
    map["language"] = toString(properties.language);
    map["timeFormat24h"] = properties.timeFormat24h ? "true" : "false";
    map["dateFormat"] = properties.dateFormat;

    if (!file::savePropertiesFile(file_path, map)) {
        LOGGER.error("Failed to save {}", file_path);
        return false;
    }

    // Update local cache
    cachedSettings = properties;
    cached = true;
    return true;
}

}