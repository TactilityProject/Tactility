// SPDX-License-Identifier: Apache-2.0
#include <wifi/wifi_settings.h>
#include <wifi/private/wifi_service.h>

#include <crypt/crypt.h>
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

constexpr auto* TAG = "wifi_settings";

constexpr auto* AP_FILE_SUFFIX = ".ap.properties";
constexpr auto* AP_PROPERTIES_KEY_SSID = "ssid";
constexpr auto* AP_PROPERTIES_KEY_PASSWORD = "password";
constexpr auto* AP_PROPERTIES_KEY_AUTO_CONNECT = "autoConnect";
constexpr auto* AP_PROPERTIES_KEY_CHANNEL = "channel";

constexpr auto* SETTINGS_FILE_NAME = "settings.properties";
constexpr auto* SETTINGS_KEY_ENABLE_ON_BOOT = "enableOnBoot";

constexpr size_t CRYPT_BLOCK_SIZE = 16;
// The password is padded to a multiple of the block size before encryption
constexpr size_t ENCRYPTED_PASSWORD_LIMIT = ((WIFI_SETTINGS_PASSWORD_LIMIT + CRYPT_BLOCK_SIZE - 1) / CRYPT_BLOCK_SIZE) * CRYPT_BLOCK_SIZE;
constexpr size_t ENCRYPTED_PASSWORD_HEX_LIMIT = ENCRYPTED_PASSWORD_LIMIT * 2;

static bool enable_on_boot_cached = false;
static bool enable_on_boot = false;

static bool is_file(const char* path) {
    struct stat path_stat {};
    return stat(path, &path_stat) == 0 && S_ISREG(path_stat.st_mode);
}

error_t wifi_ensure_directory_exists(const char* path) {
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

// TODO: The SSID could contain invalid filename characters (e.g. "/", "\" and more)
static error_t get_ap_file_path(const char* ssid, char* out_path, size_t out_path_size) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = service_paths_get_user_data_directory(WIFI_SERVICE_ID, directory, sizeof(directory));
    if (error != ERROR_NONE) {
        return error;
    }
    int written = snprintf(out_path, out_path_size, "%s/%s%s", directory, ssid, AP_FILE_SUFFIX);
    if (written < 0 || static_cast<size_t>(written) >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    return ERROR_NONE;
}

// The IV is derived from the SSID rather than the password/ciphertext, because the SSID is the one
// value that's known and identical at both encrypt time (save) and decrypt time (load).
static bool encrypt_password(const char* ssid, const char* password, char out_hex[ENCRYPTED_PASSWORD_HEX_LIMIT + 1]) {
    const size_t length = strlen(password);
    const size_t encrypted_length = ((length + CRYPT_BLOCK_SIZE - 1) / CRYPT_BLOCK_SIZE) * CRYPT_BLOCK_SIZE;

    uint8_t padded_password[ENCRYPTED_PASSWORD_LIMIT] = {};
    memcpy(padded_password, password, length);

    uint8_t encrypted[ENCRYPTED_PASSWORD_LIMIT];
    uint8_t iv[16];
    crypt_get_iv(ssid, strlen(ssid), iv);
    if (crypt_encrypt(iv, padded_password, encrypted, encrypted_length) != 0) {
        LOG_E(TAG, "Failed to encrypt");
        return false;
    }

    for (size_t i = 0; i < encrypted_length; i++) {
        snprintf(&out_hex[i * 2], 3, "%02x", encrypted[i]);
    }
    out_hex[encrypted_length * 2] = '\0';
    return true;
}

static bool decrypt_password(const char* ssid, const char* hex, char out_password[WIFI_SETTINGS_PASSWORD_LIMIT + 1]) {
    const size_t hex_length = strlen(hex);
    if (hex_length % 2 != 0 || hex_length > ENCRYPTED_PASSWORD_HEX_LIMIT) {
        LOG_E(TAG, "Invalid encrypted password length");
        return false;
    }

    const size_t encrypted_length = hex_length / 2;
    uint8_t encrypted[ENCRYPTED_PASSWORD_LIMIT];
    char byte_hex[3] = {};
    for (size_t i = 0; i < encrypted_length; i++) {
        byte_hex[0] = hex[i * 2];
        byte_hex[1] = hex[i * 2 + 1];
        encrypted[i] = static_cast<uint8_t>(strtoul(byte_hex, nullptr, 16));
    }

    uint8_t iv[16];
    crypt_get_iv(ssid, strlen(ssid), iv);

    // Space for the null terminator
    uint8_t decrypted[ENCRYPTED_PASSWORD_LIMIT + 1] = {};
    int result = crypt_decrypt(iv, encrypted, decrypted, encrypted_length);
    if (result != 0) {
        LOG_E(TAG, "Failed to decrypt credentials for \"%s\": %d", ssid, result);
        return false;
    }

    strncpy(out_password, reinterpret_cast<char*>(decrypted), WIFI_SETTINGS_PASSWORD_LIMIT);
    out_password[WIFI_SETTINGS_PASSWORD_LIMIT] = '\0';
    return true;
}

extern "C" {

bool wifi_settings_contains(const char* ssid) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    return get_ap_file_path(ssid, path, sizeof(path)) == ERROR_NONE && is_file(path);
}

error_t wifi_settings_load(const char* ssid, WifiApSettings* settings) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = get_ap_file_path(ssid, path, sizeof(path));
    if (error != ERROR_NONE) {
        return error;
    }
    if (!is_file(path)) {
        return ERROR_NOT_FOUND;
    }

    PropertiesFile* file = properties_file_open(path);
    if (file == nullptr) {
        LOG_E(TAG, "Failed to load properties from %s", path);
        return ERROR_RESOURCE;
    }

    WifiApSettings loaded = {
        .ssid = {},
        .password = {},
        .auto_connect = true,
        .channel = 0
    };
    char value[ENCRYPTED_PASSWORD_HEX_LIMIT + 1];
    error = ERROR_NONE;

    if (properties_file_get(file, AP_PROPERTIES_KEY_SSID, loaded.ssid, sizeof(loaded.ssid)) != ERROR_NONE) {
        LOG_E(TAG, "File does not contain a valid SSID: %s", path);
        error = ERROR_RESOURCE;
    } else if (properties_file_get(file, AP_PROPERTIES_KEY_PASSWORD, value, sizeof(value)) == ERROR_NONE &&
        value[0] != '\0' &&
        !decrypt_password(ssid, value, loaded.password)) {
        LOG_E(TAG, "Failed to decrypt password from %s", path);
        error = ERROR_RESOURCE;
    } else {
        if (properties_file_get(file, AP_PROPERTIES_KEY_AUTO_CONNECT, value, sizeof(value)) == ERROR_NONE) {
            loaded.auto_connect = strcmp(value, "true") == 0;
        }
        if (properties_file_get(file, AP_PROPERTIES_KEY_CHANNEL, value, sizeof(value)) == ERROR_NONE) {
            loaded.channel = static_cast<int32_t>(strtol(value, nullptr, 10));
        }
    }

    properties_file_close(file);

    if (error == ERROR_NONE) {
        *settings = loaded;
    }
    return error;
}

error_t wifi_settings_save(const WifiApSettings* settings) {
    if (settings->ssid[0] == '\0') {
        return ERROR_INVALID_ARGUMENT;
    }

    char directory[FILE_MAX_PATH_STRING_LENGTH];
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_directory(WIFI_SERVICE_ID, directory, sizeof(directory)) != ERROR_NONE ||
        get_ap_file_path(settings->ssid, path, sizeof(path)) != ERROR_NONE) {
        return ERROR_RESOURCE;
    }

    if (wifi_ensure_directory_exists(directory) != ERROR_NONE) {
        LOG_E(TAG, "Failed to create %s", directory);
        return ERROR_RESOURCE;
    }

    char password_encrypted[ENCRYPTED_PASSWORD_HEX_LIMIT + 1] = {};
    if (settings->password[0] != '\0' && !encrypt_password(settings->ssid, settings->password, password_encrypted)) {
        return ERROR_RESOURCE;
    }

    PropertiesFile* file = properties_file_open(path);
    if (file == nullptr) {
        LOG_E(TAG, "Failed to open %s", path);
        return ERROR_RESOURCE;
    }

    char channel[12];
    snprintf(channel, sizeof(channel), "%ld", static_cast<long>(settings->channel));

    properties_file_set(file, AP_PROPERTIES_KEY_SSID, settings->ssid);
    properties_file_set(file, AP_PROPERTIES_KEY_PASSWORD, password_encrypted);
    properties_file_set(file, AP_PROPERTIES_KEY_AUTO_CONNECT, settings->auto_connect ? "true" : "false");
    properties_file_set(file, AP_PROPERTIES_KEY_CHANNEL, channel);

    return properties_file_close(file);
}

error_t wifi_settings_remove(const char* ssid) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = get_ap_file_path(ssid, path, sizeof(path));
    if (error != ERROR_NONE) {
        return error;
    }
    if (!is_file(path)) {
        return ERROR_NOT_FOUND;
    }
    return remove(path) == 0 ? ERROR_NONE : ERROR_RESOURCE;
}

void wifi_settings_for_each(void* context, bool (*on_ssid)(const char* ssid, void* context)) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_directory(WIFI_SERVICE_ID, directory, sizeof(directory)) != ERROR_NONE) {
        return;
    }

    DIR* dir = opendir(directory);
    if (dir == nullptr) {
        return; // No settings saved yet
    }

    const size_t suffix_length = strlen(AP_FILE_SUFFIX);
    dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        size_t name_length = strlen(entry->d_name);
        if (name_length <= suffix_length || strcmp(entry->d_name + name_length - suffix_length, AP_FILE_SUFFIX) != 0) {
            continue;
        }

        char path[FILE_MAX_PATH_STRING_LENGTH];
        int written = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        if (written < 0 || static_cast<size_t>(written) >= sizeof(path) || !is_file(path)) {
            continue;
        }

        PropertiesFile* file = properties_file_open(path);
        if (file == nullptr) {
            continue;
        }
        char ssid[WIFI_SETTINGS_SSID_LIMIT + 1];
        bool has_ssid = properties_file_get(file, AP_PROPERTIES_KEY_SSID, ssid, sizeof(ssid)) == ERROR_NONE;
        properties_file_close(file);

        if (has_ssid && !on_ssid(ssid, context)) {
            break;
        }
    }
    closedir(dir);
}

error_t wifi_settings_set_enable_on_boot(bool enable) {
    enable_on_boot = enable;
    enable_on_boot_cached = true;

    char directory[FILE_MAX_PATH_STRING_LENGTH];
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_directory(WIFI_SERVICE_ID, directory, sizeof(directory)) != ERROR_NONE ||
        service_paths_get_user_data_path(WIFI_SERVICE_ID, SETTINGS_FILE_NAME, path, sizeof(path)) != ERROR_NONE) {
        return ERROR_RESOURCE;
    }

    if (wifi_ensure_directory_exists(directory) != ERROR_NONE) {
        LOG_E(TAG, "Failed to create %s", directory);
        return ERROR_RESOURCE;
    }

    PropertiesFile* file = properties_file_open(path);
    if (file == nullptr) {
        LOG_E(TAG, "Failed to open %s", path);
        return ERROR_RESOURCE;
    }
    properties_file_set(file, SETTINGS_KEY_ENABLE_ON_BOOT, enable ? "true" : "false");
    return properties_file_close(file);
}

bool wifi_settings_get_enable_on_boot(void) {
    if (enable_on_boot_cached) {
        return enable_on_boot;
    }

    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_path(WIFI_SERVICE_ID, SETTINGS_FILE_NAME, path, sizeof(path)) != ERROR_NONE || !is_file(path)) {
        return enable_on_boot;
    }

    PropertiesFile* file = properties_file_open(path);
    if (file == nullptr) {
        LOG_I(TAG, "Failed to load settings, using defaults");
        return enable_on_boot;
    }

    char value[8];
    if (properties_file_get(file, SETTINGS_KEY_ENABLE_ON_BOOT, value, sizeof(value)) == ERROR_NONE) {
        enable_on_boot = strcmp(value, "true") == 0;
        enable_on_boot_cached = true;
    } else {
        LOG_I(TAG, "Failed to load settings, using defaults");
    }
    properties_file_close(file);
    return enable_on_boot;
}

}
