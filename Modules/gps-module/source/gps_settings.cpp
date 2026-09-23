// SPDX-License-Identifier: Apache-2.0
#include <gps/gps_settings.h>

#include "tactility/paths.h"

#include <gps/private/gps_ledger.h>

#include <service/paths.h>

#include <tactility/log.h>

#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <vector>

constexpr auto* TAG = "gps_settings";

// Storage key for the persisted configuration file (services would use their own service ID for
// this; gps_settings has no service backing it, so it defines its own).
constexpr auto* GPS_SETTINGS_STORAGE_ID = "tactility.gps";

// region Configuration persistence

// Recursively creates every missing directory component of `path` (best-effort - mkdir() failures
// other than "already exists" are surfaced later, when the actual config file open fails).
static void ensure_directory_exists(const char* path) {
    char buffer[224];
    std::strncpy(buffer, path, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    for (char* p = buffer + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buffer, 0777);
            *p = '/';
        }
    }
    mkdir(buffer, 0777);
}

static bool get_configuration_path(char* out_path, size_t out_path_size) {
    return service_paths_get_user_data_path(GPS_SETTINGS_STORAGE_ID, "config.bin", out_path, out_path_size) == ERROR_NONE;
}

void gps_settings_for_each_configuration(void* context, void (*on_configuration)(const GpsConfiguration* configuration, size_t index, void* context)) {
    char path[224];
    if (!get_configuration_path(path, sizeof(path))) {
        return;
    }

    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return; // No configurations saved yet
    }

    GpsConfiguration configuration;
    size_t index = 0;
    while (fread(&configuration, sizeof(configuration), 1, file) == 1) {
        on_configuration(&configuration, index, context);
        index++;
    }

    fclose(file);
}

static void collect_configuration(const GpsConfiguration* configuration, size_t, void* context) {
    static_cast<std::vector<GpsConfiguration>*>(context)->push_back(*configuration);
}

static void load_configurations(std::vector<GpsConfiguration>& out) {
    gps_settings_for_each_configuration(&out, collect_configuration);
}

static error_t write_configurations(const std::vector<GpsConfiguration>& configurations) {
    char directory[224];
    if (service_paths_get_user_data_directory(GPS_SETTINGS_STORAGE_ID, directory, sizeof(directory)) != ERROR_NONE) {
        return ERROR_RESOURCE;
    }

    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!get_configuration_path(path, sizeof(path))) {
        return ERROR_RESOURCE;
    }

    ensure_directory_exists(directory);

    FILE* file = fopen(path, "wb");
    if (file == nullptr) {
        LOG_E(TAG, "Failed to open %s for writing", path);
        return ERROR_RESOURCE;
    }

    bool ok = true;
    for (auto& configuration : configurations) {
        if (fwrite(&configuration, sizeof(configuration), 1, file) != 1) {
            ok = false;
            break;
        }
    }

    fclose(file);
    if (!ok) {
        return ERROR_RESOURCE;
    }

    gps_ledger_sync();

    return ERROR_NONE;
}

// endregion

error_t gps_settings_add_configuration(const GpsConfiguration* configuration) {
    std::vector<GpsConfiguration> configurations;
    load_configurations(configurations);
    configurations.push_back(*configuration);

    error_t error = write_configurations(configurations);
    if (error != ERROR_NONE) {
        return error;
    }

    return ERROR_NONE;
}

error_t gps_settings_remove_configuration_at(size_t index) {
    std::vector<GpsConfiguration> configurations;
    load_configurations(configurations);

    if (index >= configurations.size()) {
        return ERROR_NOT_FOUND;
    }

    configurations.erase(configurations.begin() + static_cast<ptrdiff_t>(index));

    error_t error = write_configurations(configurations);
    if (error != ERROR_NONE) {
        return error;
    }

    return ERROR_NONE;
}
