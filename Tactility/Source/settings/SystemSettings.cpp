#include <Tactility/Mutex.h>
#include <Tactility/file/File.h>
#include <Tactility/file/FileLock.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/settings/Language.h>
#include <Tactility/settings/SystemSettings.h>

#include "Tactility/Paths.h"

#include <tactility/log.h>

#include <format>

namespace tt::settings {

constexpr auto* TAG = "SystemSettings";

constexpr auto* FILE_PATH_FORMAT = "{}/settings/system.properties";

static bool cached = false;
static SystemSettings cachedSettings;

static bool hasSystemSettingsFile() {
    auto file_path = std::format(FILE_PATH_FORMAT, getUserDataPath());
    return file::isFile(file_path);
}

static bool loadSystemSettingsFromFile(SystemSettings& properties) {
    auto file_path = std::format(FILE_PATH_FORMAT, getUserDataPath());
    LOG_I(TAG, "System settings loading from %s", file_path.c_str());
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(file_path, map)) {
        return false;
    }

    auto language_entry = map.find("language");
    if (language_entry != map.end()) {
        if (!fromString(language_entry->second, properties.language)) {
            LOG_W(TAG, "Unknown language \"%s\" in %s", language_entry->second.c_str(), file_path.c_str());
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
        LOG_I(TAG, "dateFormat missing or empty, using default MM/DD/YYYY (likely from older system.properties)");
        properties.dateFormat = "MM/DD/YYYY";
    }

    LOG_I(TAG, "System settings loaded");
    return true;
}

bool loadSystemSettings(SystemSettings& properties) {
    if (!cached && hasSystemSettingsFile()) {
        if (loadSystemSettingsFromFile(cachedSettings)) {
            cached = true;
        } else {
            LOG_E(TAG, "Failed to load");
        }
    }

    properties = cachedSettings;
    return true;
}

bool saveSystemSettings(const SystemSettings& properties) {
    auto file_path = std::format(FILE_PATH_FORMAT, getUserDataPath());
    std::map<std::string, std::string> map;
    map["language"] = toString(properties.language);
    map["timeFormat24h"] = properties.timeFormat24h ? "true" : "false";
    map["dateFormat"] = properties.dateFormat;

    if (!file::findOrCreateParentDirectory(file_path, 0755)) {
        LOG_E(TAG, "Failed to create parent dir for %s", file_path.c_str());
        return false;
    }

    if (!file::savePropertiesFile(file_path, map)) {
        LOG_E(TAG, "Failed to save %s", file_path.c_str());
        return false;
    }

    // Update local cache
    cachedSettings = properties;
    cached = true;
    return true;
}

}