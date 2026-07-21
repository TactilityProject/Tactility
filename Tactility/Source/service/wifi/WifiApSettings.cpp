#include <Tactility/service/wifi/WifiPrivate.h>
#include <Tactility/service/wifi/WifiApSettings.h>
#include <Tactility/file/PropertiesFile.h>

#include <Tactility/file/File.h>

#include <tactility/crypt.h>

#include <Tactility/service/ServicePaths.h>

#include <tactility/log.h>

#include <cstring>
#include <format>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <string>

namespace tt::service::wifi::settings {

constexpr auto* TAG = "WifiApSettings";

constexpr auto* AP_SETTINGS_FORMAT = "{}/{}.ap.properties";

constexpr auto* AP_PROPERTIES_KEY_SSID = "ssid";
constexpr auto* AP_PROPERTIES_KEY_PASSWORD = "password";
constexpr auto* AP_PROPERTIES_KEY_AUTO_CONNECT = "autoConnect";
constexpr auto* AP_PROPERTIES_KEY_CHANNEL = "channel";

std::string toHexString(const uint8_t *data, int length) {
    std::stringstream stream;
    stream << std::hex;
    for( int i(0) ; i < length; ++i )
        stream << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    return stream.str();
}

bool readHex(const std::string& input, uint8_t* buffer, int length) {
    if (input.size() / 2 != length) {
        LOG_E(TAG, "readHex() length mismatch");
        return false;
    }

    char hex[3] = { 0 };
    for (int i = 0; i < length; i++) {
        hex[0] = input[i * 2];
        hex[1] = input[i * 2 + 1];
        char* endptr;
        buffer[i] = static_cast<uint8_t>(strtoul(hex, &endptr, 16));
    }

    return true;
}

// TODO: The SSID could contain invalid filename characters (e.g. "/", "\" and more) so we have to refactor this.
static std::string getApPropertiesFilePath(std::shared_ptr<ServicePaths> paths, const std::string& ssid) {
    return std::format(AP_SETTINGS_FORMAT, paths->getUserDataDirectory(), ssid);
}

// The IV is derived from the SSID rather than the password/ciphertext, because the SSID is the one
// value that's known and identical at both encrypt time (save) and decrypt time (load).
static bool encrypt(const std::string& ssid, const std::string& plaintext, std::string& ciphertextOutput) {
    uint8_t iv[16];
    const auto length = plaintext.size();
    constexpr size_t chunk_size = 16;
    const auto encrypted_length = ((length / chunk_size) + (length % chunk_size ? 1 : 0)) * chunk_size;

    // crypt_encrypt reads encrypted_length bytes, but plaintext.c_str() only guarantees length + 1 bytes,
    // so pad the input into a zero-filled buffer of encrypted_length first to avoid reading past it.
    auto* padded_plaintext = static_cast<uint8_t*>(calloc(encrypted_length, 1));
    memcpy(padded_plaintext, plaintext.c_str(), length);

    auto* buffer = static_cast<uint8_t*>(malloc(encrypted_length));

    crypt_get_iv(ssid.c_str(), ssid.size(), iv);
    if (crypt_encrypt(iv, padded_plaintext, buffer, encrypted_length) != 0) {
        LOG_E(TAG, "Failed to encrypt");
        free(padded_plaintext);
        free(buffer);
        return false;
    }

    free(padded_plaintext);
    ciphertextOutput = toHexString(buffer, encrypted_length);
    free(buffer);

    return true;
}

static bool decrypt(const std::string& ssid, const std::string& ciphertextInput, std::string& plaintextOutput) {
    assert(!ciphertextInput.empty());
    assert(ciphertextInput.size() % 2 == 0);
    auto* data = static_cast<uint8_t*>(malloc(ciphertextInput.size() / 2));
    if (!readHex(ciphertextInput, data, ciphertextInput.size() / 2)) {
        LOG_E(TAG, "Failed to read hex");
        free(data);
        return false;
    }

    uint8_t iv[16];
    crypt_get_iv(ssid.c_str(), ssid.size(), iv);

    auto result_length = ciphertextInput.size() / 2;
    // Allocate correct length plus space for string null terminator
    auto* result = static_cast<uint8_t*>(malloc(result_length + 1));
    result[result_length] = 0;

    int decrypt_result = crypt_decrypt(
        iv,
        data,
        result,
        ciphertextInput.size() / 2
    );

    free(data);

    if (decrypt_result != 0) {
        LOG_E(TAG, "Failed to decrypt credentials for \"%s\": %d", ssid.c_str(), decrypt_result);
        free(result);
        return false;
    }

    plaintextOutput = reinterpret_cast<char*>(result);
    free(result);
    return true;
}

bool contains(const std::string& ssid) {
    auto service_context = findServiceContext();
    if (service_context == nullptr) {
        return false;
    }
    const auto file_path = getApPropertiesFilePath(service_context->getPaths(), ssid);
    return file::isFile(file_path);
}

bool load(const std::string& ssid, WifiApSettings& apSettings) {
    auto service_context = findServiceContext();
    if (service_context == nullptr) {
        return false;
    }
    const auto file_path = getApPropertiesFilePath(service_context->getPaths(), ssid);
    if (!file::isFile(file_path)) {
        return false;
    }

    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(file_path, map)) {
        return false;
    }

    // SSID is required
    if (!map.contains(AP_PROPERTIES_KEY_SSID)) {
        return false;
    }

    apSettings.ssid = map[AP_PROPERTIES_KEY_SSID];
    assert(ssid == apSettings.ssid);

    if (map.contains(AP_PROPERTIES_KEY_PASSWORD)) {
        std::string password_decrypted;
        const auto& encrypted_password = map[AP_PROPERTIES_KEY_PASSWORD];
        if (encrypted_password.empty()) {
            apSettings.password = "";
        } else if (decrypt(ssid, encrypted_password, password_decrypted)) {
            apSettings.password = password_decrypted;
        } else {
            return false;
        }
    } else {
        apSettings.password = "";
    }

    if (map.contains(AP_PROPERTIES_KEY_AUTO_CONNECT)) {
        apSettings.autoConnect = (map[AP_PROPERTIES_KEY_AUTO_CONNECT] == "true");
    } else {
        apSettings.autoConnect = true;
    }

    if (map.contains(AP_PROPERTIES_KEY_CHANNEL)) {
        apSettings.channel = std::stoi(map[AP_PROPERTIES_KEY_CHANNEL]);
    } else {
        apSettings.channel = 0;
    }

    return true;

}

bool save(const WifiApSettings& apSettings) {
    if (apSettings.ssid.empty()) {
        return false;
    }

    auto service_context = findServiceContext();
    if (service_context == nullptr) {
        return false;
    }

    const auto file_path = getApPropertiesFilePath(service_context->getPaths(), apSettings.ssid);
    if (!file::findOrCreateParentDirectory(file_path, 0755)) {
        LOG_E(TAG, "Failed to create %s", file_path.c_str());
        return false;
    }

    std::map<std::string, std::string> map;

    std::string password_encrypted;
    if (!apSettings.password.empty()) {
        if (!encrypt(apSettings.ssid, apSettings.password, password_encrypted)) {
            return false;
        }
    } else {
        password_encrypted = "";
    }

    map[AP_PROPERTIES_KEY_PASSWORD] = password_encrypted;
    map[AP_PROPERTIES_KEY_SSID] = apSettings.ssid;
    map[AP_PROPERTIES_KEY_AUTO_CONNECT] = apSettings.autoConnect ? "true" : "false";
    map[AP_PROPERTIES_KEY_CHANNEL] = std::to_string(apSettings.channel);

    return file::savePropertiesFile(file_path, map);
}

bool remove(const std::string& ssid) {
    auto service_context = findServiceContext();
    if (service_context == nullptr) {
        return false;
    }

    const auto path = getApPropertiesFilePath(service_context->getPaths(), ssid);
    if (!file::isFile(path)) {
        return false;
    }
    return ::remove(path.c_str()) == 0;
}

}
