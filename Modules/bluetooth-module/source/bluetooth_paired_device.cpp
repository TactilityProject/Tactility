// SPDX-License-Identifier: Apache-2.0
#include <bluetooth/bluetooth_paired_device.h>
#include <bluetooth/private/bluetooth_service.h>

#include <service/paths.h>

#include <tactility/log.h>
#include <tactility/paths.h>
#include <tactility/properties_file.h>

#include <dirent.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

constexpr auto* TAG = "bluetooth_paired_device";

constexpr auto* FILE_SUFFIX = ".device.properties";
constexpr auto* KEY_NAME = "name";
constexpr auto* KEY_ADDR = "addr";
constexpr auto* KEY_AUTO_CONNECT = "autoConnect";
constexpr auto* KEY_PROFILE_ID = "profileId";

// BT_PROFILE_SPP, for devices saved without a profile
constexpr int DEFAULT_PROFILE = 2;

error_t bluetooth_ensure_directory_exists(const char* path) {
    char buffer[FILE_MAX_PATH_STRING_LENGTH];
    if (strlen(path) >= sizeof(buffer)) {
        return ERROR_BUFFER_OVERFLOW;
    }
    strcpy(buffer, path);

    for (char* p = buffer + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
                return ERROR_RESOURCE;
            }
            *p = '/';
        }
    }
    if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

static bool is_file(const char* path) {
    struct stat path_stat {};
    return stat(path, &path_stat) == 0 && S_ISREG(path_stat.st_mode);
}

static error_t get_file_path(const BtAddr addr, char* out_path, size_t out_path_size) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = service_paths_get_user_data_directory(BLUETOOTH_SERVICE_ID, directory, sizeof(directory));
    if (error != ERROR_NONE) {
        return error;
    }
    char addr_hex[BLUETOOTH_ADDR_HEX_LENGTH + 1];
    bluetooth_addr_to_hex(addr, addr_hex);
    int written = snprintf(out_path, out_path_size, "%s/%s%s", directory, addr_hex, FILE_SUFFIX);
    if (written < 0 || static_cast<size_t>(written) >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
}

static error_t load_from_path(const char* path, struct BluetoothPairedDevice* device) {
    PropertiesFile* file = properties_file_open(path);
    if (file == nullptr) {
        return ERROR_RESOURCE;
    }

    struct BluetoothPairedDevice loaded = {};
    loaded.auto_connect = true;
    loaded.profile = DEFAULT_PROFILE;

    char value[BT_NAME_MAX + 1];
    error_t error = ERROR_NONE;
    if (properties_file_get(file, KEY_ADDR, value, sizeof(value)) != ERROR_NONE || !bluetooth_hex_to_addr(value, loaded.addr)) {
        error = ERROR_RESOURCE;
    } else {
        if (properties_file_get(file, KEY_NAME, loaded.name, sizeof(loaded.name)) != ERROR_NONE) {
            loaded.name[0] = '\0';
        }
        if (properties_file_get(file, KEY_AUTO_CONNECT, value, sizeof(value)) == ERROR_NONE) {
            loaded.auto_connect = strcmp(value, "true") == 0;
        }
        if (properties_file_get(file, KEY_PROFILE_ID, value, sizeof(value)) == ERROR_NONE) {
            loaded.profile = static_cast<int>(strtol(value, nullptr, 10));
        }
    }
    properties_file_close(file);

    if (error == ERROR_NONE) {
        *device = loaded;
    }
    return error;
}

extern "C" {

void bluetooth_addr_to_hex(const BtAddr addr, char* out_hex) {
    for (int i = 0; i < BT_ADDR_LEN; i++) {
        snprintf(&out_hex[i * 2], 3, "%02x", addr[i]);
    }
}

bool bluetooth_hex_to_addr(const char* hex, BtAddr addr) {
    if (strlen(hex) != BLUETOOTH_ADDR_HEX_LENGTH) {
        LOG_E(TAG, "Invalid address length: %s", hex);
        return false;
    }
    char buffer[3] = {};
    for (int i = 0; i < BT_ADDR_LEN; i++) {
        buffer[0] = hex[i * 2];
        buffer[1] = hex[i * 2 + 1];
        char* end = nullptr;
        addr[i] = static_cast<uint8_t>(strtoul(buffer, &end, 16));
        if (end != buffer + 2) {
            LOG_E(TAG, "Invalid hex at byte %d: %s", i, hex);
            return false;
        }
    }
    return true;
}

bool bluetooth_paired_device_exists(const BtAddr addr) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    return get_file_path(addr, path, sizeof(path)) == ERROR_NONE && is_file(path);
}

error_t bluetooth_paired_device_load(const BtAddr addr, struct BluetoothPairedDevice* device) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = get_file_path(addr, path, sizeof(path));
    if (error != ERROR_NONE) {
        return error;
    }
    if (!is_file(path)) {
        return ERROR_NOT_FOUND;
    }
    return load_from_path(path, device);
}

error_t bluetooth_paired_device_save(const struct BluetoothPairedDevice* device) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_directory(BLUETOOTH_SERVICE_ID, directory, sizeof(directory)) != ERROR_NONE ||
        get_file_path(device->addr, path, sizeof(path)) != ERROR_NONE) {
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

    char addr_hex[BLUETOOTH_ADDR_HEX_LENGTH + 1];
    bluetooth_addr_to_hex(device->addr, addr_hex);
    char profile[12];
    snprintf(profile, sizeof(profile), "%d", device->profile);

    properties_file_set(file, KEY_NAME, device->name);
    properties_file_set(file, KEY_ADDR, addr_hex);
    properties_file_set(file, KEY_AUTO_CONNECT, device->auto_connect ? "true" : "false");
    properties_file_set(file, KEY_PROFILE_ID, profile);

    return properties_file_close(file);
}

error_t bluetooth_paired_device_remove(const BtAddr addr) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = get_file_path(addr, path, sizeof(path));
    if (error != ERROR_NONE) {
        return error;
    }
    if (!is_file(path)) {
        return ERROR_NOT_FOUND;
    }
    return remove(path) == 0 ? ERROR_NONE : ERROR_RESOURCE;
}

void bluetooth_paired_device_for_each(void* context, bool (*on_device)(const struct BluetoothPairedDevice* device, void* context)) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_directory(BLUETOOTH_SERVICE_ID, directory, sizeof(directory)) != ERROR_NONE) {
        return;
    }

    DIR* dir = opendir(directory);
    if (dir == nullptr) {
        return; // No devices saved yet
    }

    const size_t suffix_length = strlen(FILE_SUFFIX);
    dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        size_t name_length = strlen(entry->d_name);
        if (name_length <= suffix_length || strcmp(entry->d_name + name_length - suffix_length, FILE_SUFFIX) != 0) {
            continue;
        }

        char path[FILE_MAX_PATH_STRING_LENGTH];
        int written = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (written < 0 || static_cast<size_t>(written) >= sizeof(path) || !is_file(path)) {
            continue;
        }

        struct BluetoothPairedDevice device;
        if (load_from_path(path, &device) == ERROR_NONE && !on_device(&device, context)) {
            break;
        }
    }
    closedir(dir);
}

}
