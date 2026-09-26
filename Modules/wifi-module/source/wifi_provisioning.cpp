// SPDX-License-Identifier: Apache-2.0
#include <wifi/wifi_settings.h>
#include <wifi/private/wifi_service.h>

#include <tactility/log.h>
#include <tactility/paths.h>
#include <tactility/properties_file.h>

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

constexpr auto* TAG = "wifi_provisioning";

constexpr auto* AP_FILE_SUFFIX = ".ap.properties";
constexpr auto* AP_PROPERTIES_KEY_SSID = "ssid";
constexpr auto* AP_PROPERTIES_KEY_PASSWORD = "password";
constexpr auto* AP_PROPERTIES_KEY_AUTO_CONNECT = "autoConnect";
constexpr auto* AP_PROPERTIES_KEY_CHANNEL = "channel";
constexpr auto* AP_PROPERTIES_KEY_AUTO_REMOVE = "autoRemovePropertiesFile";

static bool ends_with(const char* text, const char* suffix) {
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);
    return text_length >= suffix_length && strcmp(text + text_length - suffix_length, suffix) == 0;
}

static void import_ap(const char* path) {
    PropertiesFile* file = properties_file_open(path);
    if (file == nullptr) {
        LOG_E(TAG, "Failed to load AP properties at %s", path);
        return;
    }

    WifiApSettings settings = {
        .ssid = {},
        .password = {},
        .auto_connect = true,
        .channel = 0
    };
    char value[16];
    bool auto_remove = false;
    bool valid = properties_file_get(file, AP_PROPERTIES_KEY_SSID, settings.ssid, sizeof(settings.ssid)) == ERROR_NONE;
    if (valid) {
        // A missing password means an open network
        if (properties_file_get(file, AP_PROPERTIES_KEY_PASSWORD, settings.password, sizeof(settings.password)) == ERROR_BUFFER_OVERFLOW) {
            valid = false;
        }
        if (properties_file_get(file, AP_PROPERTIES_KEY_AUTO_CONNECT, value, sizeof(value)) == ERROR_NONE) {
            settings.auto_connect = strcmp(value, "true") == 0;
        }
        if (properties_file_get(file, AP_PROPERTIES_KEY_CHANNEL, value, sizeof(value)) == ERROR_NONE) {
            settings.channel = static_cast<int32_t>(strtol(value, nullptr, 10));
        }
        if (properties_file_get(file, AP_PROPERTIES_KEY_AUTO_REMOVE, value, sizeof(value)) == ERROR_NONE) {
            auto_remove = strcmp(value, "true") == 0;
        }
    }
    properties_file_close(file);

    if (!valid) {
        LOG_E(TAG, "%s is missing a valid ssid or has an invalid password", path);
        return;
    }

    if (!wifi_settings_contains(settings.ssid)) {
        if (wifi_settings_save(&settings) != ERROR_NONE) {
            LOG_E(TAG, "Failed to save settings for %s", settings.ssid);
        } else {
            LOG_I(TAG, "Imported %s from %s", settings.ssid, path);
        }
    }

    if (auto_remove) {
        if (remove(path) != 0) {
            LOG_E(TAG, "Failed to auto-remove %s", path);
        } else {
            LOG_I(TAG, "Auto-removed %s", path);
        }
    }
}

extern "C" {

void wifi_provisioning_import(void) {
    char data_path[FILE_MAX_PATH_STRING_LENGTH];
    if (paths_get_data_path(data_path, sizeof(data_path)) != ERROR_NONE) {
        LOG_I(TAG, "Skip provisioning: no data path");
        return;
    }

    char provisioning_path[FILE_MAX_PATH_STRING_LENGTH];
    int written = snprintf(provisioning_path, sizeof(provisioning_path), "%s/provisioning", data_path);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(provisioning_path)) {
        return;
    }

    DIR* dir = opendir(provisioning_path);
    if (dir == nullptr) {
        LOG_I(TAG, "Skip provisioning: no files at %s", provisioning_path);
        return;
    }

    bool found = false;
    dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (!ends_with(entry->d_name, AP_FILE_SUFFIX)) {
            continue;
        }

        char file_path[FILE_MAX_PATH_STRING_LENGTH];
        written = snprintf(file_path, sizeof(file_path), "%s/%s", provisioning_path, entry->d_name);
        if (written < 0 || static_cast<size_t>(written) >= sizeof(file_path)) {
            continue;
        }

        struct stat file_stat {};
        if (stat(file_path, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
            found = true;
            import_ap(file_path);
        }
    }
    closedir(dir);

    if (!found) {
        LOG_W(TAG, "No AP files found at %s", provisioning_path);
    }
}

}
