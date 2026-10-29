// SPDX-License-Identifier: Apache-2.0
#include <mesh/mesh_settings.h>

#include <service/paths.h>

#include <tactility/filesystem/fs.h>
#include <tactility/log.h>
#include <tactility/paths.h>
#include <tactility/properties_file.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef ESP_PLATFORM
#include <esp_random.h>
#else
#include <random>
#endif

constexpr auto* TAG = "mesh_settings";
constexpr auto* SETTINGS_FILE_NAME = "settings.properties";
constexpr auto* KEY_ENABLED = "enabled";
constexpr auto* KEY_BACKEND = "backend";
constexpr auto* KEY_PRESET = "preset";
constexpr auto* KEY_FREQUENCY = "frequency";
constexpr auto* KEY_BANDWIDTH = "bandwidth";
constexpr auto* KEY_SPREADING_FACTOR = "spreadingFactor";
constexpr auto* KEY_CODING_RATE = "codingRate";
constexpr auto* KEY_POWER = "power";
constexpr auto* KEY_PATH_HASH_SIZE = "pathHashSize";
constexpr auto* KEY_REGION_SCOPE = "regionScope";
constexpr auto* KEY_NODE_NAME = "nodeName";
constexpr auto* KEY_RADIO = "radio";
// Earlier versions stored this as the name when the user hadn't chosen one
constexpr auto* LEGACY_DEFAULT_NODE_NAME = "Tactility";

static uint32_t random_u32() {
#ifdef ESP_PLATFORM
    return esp_random();
#else
    std::random_device device;
    return device();
#endif
}

static void read_settings_file(MeshSettings* settings) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_path(MESH_SERVICE_ID, SETTINGS_FILE_NAME, path, sizeof(path)) != ERROR_NONE) {
        return;
    }

    PropertiesFile* file = properties_file_open(path);
    if (file == nullptr) {
        return;
    }

    char value[64];
    if (properties_file_get(file, KEY_ENABLED, value, sizeof(value)) == ERROR_NONE) {
        settings->enabled = strcmp(value, "true") == 0;
    }
    if (properties_file_get(file, KEY_BACKEND, value, sizeof(value)) == ERROR_NONE) {
        strncpy(settings->backend, value, MESH_BACKEND_ID_MAX_LENGTH);
    }
    if (properties_file_get(file, KEY_NODE_NAME, value, sizeof(value)) == ERROR_NONE && value[0] != '\0') {
        strncpy(settings->node_name, value, MESH_NAME_MAX_LENGTH);
    }
    if (properties_file_get(file, KEY_RADIO, value, sizeof(value)) == ERROR_NONE) {
        strncpy(settings->radio, value, MESH_NAME_MAX_LENGTH);
    }
    properties_file_close(file);
}

void mesh_settings_load(MeshSettings* settings) {
    *settings = {};
    read_settings_file(settings);

    // A name that the user didn't choose is made up once and then kept
    if (settings->node_name[0] == '\0' || strcmp(settings->node_name, LEGACY_DEFAULT_NODE_NAME) == 0) {
        snprintf(settings->node_name, sizeof(settings->node_name), "TactilityUser-%08x", (unsigned)random_u32());
        if (mesh_settings_save(settings) != ERROR_NONE) {
            LOG_E(TAG, "Failed to save the generated name");
        }
    }
}

/** Opens the settings file for writing and makes sure that its directory exists */
static PropertiesFile* open_for_write() {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_directory(MESH_SERVICE_ID, directory, sizeof(directory)) != ERROR_NONE ||
        service_paths_get_user_data_path(MESH_SERVICE_ID, SETTINGS_FILE_NAME, path, sizeof(path)) != ERROR_NONE) {
        return nullptr;
    }
    const error_t made = directory_make(directory, true);
    if (made != ERROR_NONE && made != ERROR_ALREADY_EXISTS) {
        LOG_E(TAG, "Failed to create %s", directory);
        return nullptr;
    }
    return properties_file_open(path);
}

error_t mesh_settings_save(const MeshSettings* settings) {
    PropertiesFile* file = open_for_write();
    if (file == nullptr) {
        return ERROR_RESOURCE;
    }
    properties_file_set(file, KEY_ENABLED, settings->enabled ? "true" : "false");
    properties_file_set(file, KEY_BACKEND, settings->backend);
    properties_file_set(file, KEY_NODE_NAME, settings->node_name);
    properties_file_set(file, KEY_RADIO, settings->radio);
    return properties_file_close(file);
}

/** Reads a setting stored as "<backend>.<key>", or as "<key>" when @a legacy */
static bool read_backend_value(const PropertiesFile* file, const char* backend, const char* key, bool legacy, char* value, size_t value_size) {
    char name[MESH_BACKEND_ID_MAX_LENGTH + 24];
    snprintf(name, sizeof(name), "%s.%s", backend, key);
    if (properties_file_get(file, name, value, value_size) == ERROR_NONE) {
        return true;
    }
    return legacy && properties_file_get(file, key, value, value_size) == ERROR_NONE;
}

void mesh_settings_load_backend(const char* backend, bool migrate_legacy, MeshBackendSettings* settings) {
    *settings = {};
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_path(MESH_SERVICE_ID, SETTINGS_FILE_NAME, path, sizeof(path)) != ERROR_NONE) {
        return;
    }
    PropertiesFile* file = properties_file_open(path);
    if (file == nullptr) {
        return;
    }
    char value[64];
    if (read_backend_value(file, backend, KEY_PRESET, migrate_legacy, value, sizeof(value))) {
        strncpy(settings->preset, value, MESH_NAME_MAX_LENGTH);
    }
    const struct {
        const char* key;
        int32_t* target;
    } numbers[] = {
        { KEY_FREQUENCY, &settings->radio_settings.frequency },
        { KEY_BANDWIDTH, &settings->radio_settings.bandwidth },
        { KEY_SPREADING_FACTOR, &settings->radio_settings.spreading_factor },
        { KEY_CODING_RATE, &settings->radio_settings.coding_rate },
        { KEY_POWER, &settings->radio_settings.power },
        { KEY_PATH_HASH_SIZE, &settings->network.path_hash_size },
    };
    for (const auto& number : numbers) {
        if (read_backend_value(file, backend, number.key, migrate_legacy, value, sizeof(value))) {
            *number.target = atoi(value);
        }
    }
    if (read_backend_value(file, backend, KEY_REGION_SCOPE, migrate_legacy, value, sizeof(value))) {
        strncpy(settings->network.region_scope, value, MESH_NAME_MAX_LENGTH);
    }
    properties_file_close(file);
}

error_t mesh_settings_save_backend(const char* backend, const MeshBackendSettings* settings) {
    PropertiesFile* file = open_for_write();
    if (file == nullptr) {
        return ERROR_RESOURCE;
    }
    const struct {
        const char* key;
        int32_t value;
    } numbers[] = {
        { KEY_FREQUENCY, settings->radio_settings.frequency },
        { KEY_BANDWIDTH, settings->radio_settings.bandwidth },
        { KEY_SPREADING_FACTOR, settings->radio_settings.spreading_factor },
        { KEY_CODING_RATE, settings->radio_settings.coding_rate },
        { KEY_POWER, settings->radio_settings.power },
        { KEY_PATH_HASH_SIZE, settings->network.path_hash_size },
    };
    char name[MESH_BACKEND_ID_MAX_LENGTH + 24];
    snprintf(name, sizeof(name), "%s.%s", backend, KEY_PRESET);
    properties_file_set(file, name, settings->preset);
    for (const auto& number : numbers) {
        char value[16];
        snprintf(name, sizeof(name), "%s.%s", backend, number.key);
        snprintf(value, sizeof(value), "%d", (int)number.value);
        properties_file_set(file, name, value);
    }
    snprintf(name, sizeof(name), "%s.%s", backend, KEY_REGION_SCOPE);
    properties_file_set(file, name, settings->network.region_scope);
    return properties_file_close(file);
}
