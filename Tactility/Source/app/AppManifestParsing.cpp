#include <Tactility/app/AppManifestParsing.h>
#include <Tactility/app/AppManifestParsingInternal.h>

#include <Tactility/StringUtils.h>
#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>

#include <algorithm>
#include <tactility/log.h>

namespace tt::app {

constexpr auto* TAG = "AppManifest";

constexpr bool validateString(const std::string& value, const std::function<bool(char)>& isValidChar) {
    return std::ranges::all_of(value, isValidChar);
}

bool getValueFromManifest(const std::map<std::string, std::string>& map, const std::string& key, std::string& output) {
    const auto iterator = map.find(key);
    if (iterator == map.end()) {
        LOG_E(TAG, "Failed to find %s in manifest", key.c_str());
        return false;
    }
    output = iterator->second;
    return true;
}

bool isValidId(const std::string& id) {
    return id.size() >= 5 && validateString(id, [](const char c) {
        return std::isalnum(c) != 0 || c == '.';
    });
}

bool isValidManifestVersion(const std::string& version) {
    return !version.empty() && validateString(version, [](const char c) {
        return std::isalnum(c) != 0 || c == '.';
    });
}

bool isValidAppVersionName(const std::string& version) {
    return !version.empty() && validateString(version, [](const char c) {
        return std::isalnum(c) != 0 || c == '.' || c == '-' || c == '_';
    });
}

bool isValidAppVersionCode(const std::string& version) {
    return !version.empty() && validateString(version, [](const char c) {
        return std::isdigit(c) != 0;
    });
}

bool isValidName(const std::string& name) {
    return name.size() >= 2 && validateString(name, [](const char c) {
        return std::isalnum(c) != 0 || c == ' ' || c == '-';
    });
}

/** The V1 format's first line is always the literal "[manifest]" section header; V2 files are flat from the first line onward. */
static bool detectIsV1Format(const std::string& filePath) {
    std::string first_line;
    bool got_first_line = false;
    file::readLines(filePath, true, [&first_line, &got_first_line](const char* line) {
        if (!got_first_line) {
            first_line = string::trim(std::string(line), " \t\r\n");
            got_first_line = true;
        }
    });
    return first_line == "[manifest]";
}

bool parseManifest(const std::string& filePath, AppManifest& manifest) {
    LOG_I(TAG, "Parsing manifest %s", filePath.c_str());

    bool is_v1_format = detectIsV1Format(filePath);

    std::map<std::string, std::string> properties;
    if (!file::loadPropertiesFile(filePath, properties)) {
        LOG_E(TAG, "Failed to load manifest at %s", filePath.c_str());
        return false;
    }

    bool success = is_v1_format
        ? parseManifestV1(properties, manifest)
        : parseManifestV2(properties, manifest);

    if (!success) {
        return false;
    }

    manifest.appCategory = Category::User;
    manifest.appLocation = Location::external("");

    return true;
}

}
