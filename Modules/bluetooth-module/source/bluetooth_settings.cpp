// SPDX-License-Identifier: Apache-2.0
#include <bluetooth/bluetooth_settings.h>
#include <bluetooth/private/bluetooth_service.h>

#include <service/paths.h>

#include <tactility/log.h>
#include <tactility/paths.h>
#include <tactility/properties_file.h>

#include <sys/stat.h>

#include <cstring>

constexpr auto* TAG = "bluetooth_settings";

constexpr auto* SETTINGS_FILE_NAME = "settings.properties";
constexpr auto* KEY_ENABLE_ON_BOOT = "enableOnBoot";
constexpr auto* KEY_SPP_AUTO_START = "sppAutoStart";
constexpr auto* KEY_MIDI_AUTO_START = "midiAutoStart";

struct Settings {
    bool enable_on_boot = false;
    bool spp_auto_start = false;
    bool midi_auto_start = false;
};

static Settings cached;
static bool cached_valid = false;

static bool get_settings_path(char* out_path, size_t out_path_size) {
    return service_paths_get_user_data_path(BLUETOOTH_SERVICE_ID, SETTINGS_FILE_NAME, out_path, out_path_size) == ERROR_NONE;
}

static bool read_flag(const PropertiesFile* file, const char* key) {
    char value[8];
    return properties_file_get(file, key, value, sizeof(value)) == ERROR_NONE && strcmp(value, "true") == 0;
}

static const Settings& get_cached() {
    if (cached_valid) {
        return cached;
    }

    char path[FILE_MAX_PATH_STRING_LENGTH];
    struct stat path_stat {};
    if (get_settings_path(path, sizeof(path)) && stat(path, &path_stat) == 0 && S_ISREG(path_stat.st_mode)) {
        if (PropertiesFile* file = properties_file_open(path)) {
            cached.enable_on_boot = read_flag(file, KEY_ENABLE_ON_BOOT);
            cached.spp_auto_start = read_flag(file, KEY_SPP_AUTO_START);
            cached.midi_auto_start = read_flag(file, KEY_MIDI_AUTO_START);
            properties_file_close(file);
        } else {
            LOG_I(TAG, "Failed to load settings, using defaults");
        }
    }
    cached_valid = true;
    return cached;
}

static error_t save_flag(const char* key, bool value) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_directory(BLUETOOTH_SERVICE_ID, directory, sizeof(directory)) != ERROR_NONE || !get_settings_path(path, sizeof(path))) {
        return ERROR_RESOURCE;
    }

    if (bluetooth_ensure_directory_exists(directory) != ERROR_NONE) {
        LOG_E(TAG, "Failed to create %s", directory);
        return ERROR_RESOURCE;
    }

    PropertiesFile* file = properties_file_open(path);
    if (file == nullptr) {
        LOG_E(TAG, "Failed to open %s", path);
        return ERROR_RESOURCE;
    }
    properties_file_set(file, key, value ? "true" : "false");
    return properties_file_close(file);
}

extern "C" {

error_t bluetooth_settings_set_enable(bool enable) {
    get_cached();
    cached.enable_on_boot = enable;
    return save_flag(KEY_ENABLE_ON_BOOT, enable);
}

bool bluetooth_settings_get_enable(void) {
    return get_cached().enable_on_boot;
}

error_t bluetooth_settings_set_spp_auto_start(bool enable) {
    get_cached();
    cached.spp_auto_start = enable;
    return save_flag(KEY_SPP_AUTO_START, enable);
}

bool bluetooth_settings_get_spp_auto_start(void) {
    return get_cached().spp_auto_start;
}

error_t bluetooth_settings_set_midi_auto_start(bool enable) {
    get_cached();
    cached.midi_auto_start = enable;
    return save_flag(KEY_MIDI_AUTO_START, enable);
}

bool bluetooth_settings_get_midi_auto_start(void) {
    return get_cached().midi_auto_start;
}

}
