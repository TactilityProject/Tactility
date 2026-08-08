#include "Tactility/file/PropertiesFile.h"

#include <Tactility/file/File.h>

#include <tactility/properties_file.h>

namespace tt::file {

bool loadPropertiesFile(const std::string& filePath, std::function<void(const std::string& key, const std::string& value)> callback) {
    // Matches the original semantics: a missing file is a real failure the caller checks for
    // (e.g. "no saved settings yet"), unlike properties_file_open() itself, which treats a
    // missing file as a fresh, empty store to be created on close().
    if (!isFile(filePath)) {
        return false;
    }

    PropertiesFile* file = properties_file_open(filePath.c_str());
    if (file == nullptr) {
        return false;
    }

    properties_file_for_each(file, [](const char* key, const char* value, void* context) {
        auto* typed_callback = static_cast<std::function<void(const std::string&, const std::string&)>*>(context);
        (*typed_callback)(key, value);
    }, &callback);

    properties_file_close(file);
    return true;
}

bool loadPropertiesFile(const std::string& filePath, std::map<std::string, std::string>& outProperties) {
    return loadPropertiesFile(filePath, [&outProperties](const std::string& key, const std::string& value) {
        outProperties[key] = value;
    });
}

bool savePropertiesFile(const std::string& filePath, const std::map<std::string, std::string>& properties) {
    PropertiesFile* file = properties_file_open(filePath.c_str());
    if (file == nullptr) {
        return false;
    }

    for (const auto& [key, value] : properties) {
        properties_file_set(file, key.c_str(), value.c_str());
    }

    properties_file_close(file);
    return true;
}

}
