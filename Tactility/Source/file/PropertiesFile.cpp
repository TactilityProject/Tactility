#include "Tactility/file/PropertiesFile.h"

#include <Tactility/StringUtils.h>
#include <Tactility/file/File.h>
#include <Tactility/file/FileLock.h>
#include <tactility/log.h>

namespace tt::file {

constexpr auto* TAG = "PropertiesFile";

bool getKeyValuePair(const std::string& input, std::string& key, std::string& value) {
    auto index = input.find('=');
    if (index == std::string::npos) {
        return false;
    }
    key = input.substr(0, index);
    value = input.substr(index + 1);
    return true;
}

bool loadPropertiesFile(const std::string& filePath, std::function<void(const std::string& key, const std::string& value)> callback) {
    // Reading properties is a common operation; make this debug-level to avoid
    // flooding the serial console under frequent polling.
    LOG_D(TAG, "Reading properties file %s", filePath.c_str());
    uint16_t line_count = 0;
    std::string key_prefix = "";
    // Malformed lines are skipped, valid lines are loaded and callback is called
    return readLines(filePath, true, [&key_prefix, &line_count, &filePath, &callback](const std::string& line) {
        line_count++;
        std::string key, value;
        // Trim all whitespace including \r\n (Windows line endings)
        auto trimmed_line = string::trim(line, " \t\r\n");
        if (!trimmed_line.starts_with("#") && !trimmed_line.empty()) {
            if (trimmed_line.starts_with("[")) {
                key_prefix = trimmed_line;
            } else {
                if (getKeyValuePair(trimmed_line, key, value)) {
                   std::string trimmed_key = key_prefix + string::trim(key, " \t");
                   std::string trimmed_value = string::trim(value, " \t");
                   callback(trimmed_key, trimmed_value);
               } else {
                   LOG_E(TAG, "Failed to parse line %d of %s (skipped)", line_count, filePath.c_str());
                   // Continue loading other lines
               }
            }
        }
    });
}

bool loadPropertiesFile(const std::string& filePath, std::map<std::string, std::string>& outProperties) {
    return loadPropertiesFile(filePath, [&outProperties](const std::string& key, const std::string& value) {
        outProperties[key] = value;
    });
}

bool savePropertiesFile(const std::string& filePath, const std::map<std::string, std::string>& properties) {
    bool result = false;
    getLock(filePath)->withLock([&result, filePath, &properties] {
        LOG_I(TAG, "Saving properties file %s", filePath.c_str());

        FILE* file = fopen(filePath.c_str(), "w");
        if (file == nullptr) {
            LOG_E(TAG, "Failed to open %s", filePath.c_str());
            return;
        }

        for (const auto& [key, value]: properties) { fprintf(file, "%s=%s\n", key.c_str(), value.c_str()); }

        fclose(file);
        result = true;
    });
    return result;
}

}
