#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/ChatSettings.h>
#include <Tactility/app/chat/ChatProtocol.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/Paths.h>

#include <tactility/crypt.h>

#include <esp_random.h>

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>
#include <tactility/log.h>
#include <unistd.h>

namespace tt::app::chat {

constexpr auto* TAG = "ChatSettings";

static std::string getSettingsFilePath() {
    return getUserDataPath() + "/settings/chat.properties";
}

constexpr auto* KEY_SENDER_ID = "senderId";
constexpr auto* KEY_NICKNAME = "nickname";
constexpr auto* KEY_ENCRYPTION_KEY = "encryptionKey";
constexpr auto* KEY_CHAT_CHANNEL = "chatChannel";

uint32_t defaultSenderId = 0;

// IV_SEED provides basic obfuscation for stored encryption keys, not strong encryption.
// The device master key (from crypt_get_iv) provides the actual security.
static constexpr auto* IV_SEED = "chat_key";

static std::string toHexString(const uint8_t* data, size_t length) {
    std::stringstream stream;
    stream << std::hex;
    for (size_t i = 0; i < length; ++i) {
        stream << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return stream.str();
}

static bool readHex(const std::string& input, uint8_t* buffer, size_t length) {
    if (input.size() != length * 2) {
        LOG_E(TAG, "readHex() length mismatch");
        return false;
    }

    char hex[3] = { 0 };
    for (size_t i = 0; i < length; i++) {
        hex[0] = input[i * 2];
        hex[1] = input[i * 2 + 1];
        char* endptr;
        unsigned long val = strtoul(hex, &endptr, 16);
        if (endptr != hex + 2) {
            LOG_E(TAG, "readHex() invalid hex character");
            return false;
        }
        buffer[i] = static_cast<uint8_t>(val);
    }
    return true;
}

static bool encryptKey(const uint8_t key[ESP_NOW_KEY_LEN], std::string& hexOutput) {
    uint8_t iv[16];
    crypt_get_iv(IV_SEED, std::strlen(IV_SEED), iv);

    uint8_t encrypted[ESP_NOW_KEY_LEN];
    if (crypt_encrypt(iv, key, encrypted, ESP_NOW_KEY_LEN) != 0) {
        LOGGER.error("Failed to encrypt key");
        return false;
    }

    hexOutput = toHexString(encrypted, ESP_NOW_KEY_LEN);
    return true;
}

static bool decryptKey(const std::string& hexInput, uint8_t key[ESP_NOW_KEY_LEN]) {
    if (hexInput.size() != ESP_NOW_KEY_LEN * 2) {
        return false;
    }

    uint8_t encrypted[ESP_NOW_KEY_LEN];
    if (!readHex(hexInput, encrypted, ESP_NOW_KEY_LEN)) {
        return false;
    }

    uint8_t iv[16];
    crypt_get_iv(IV_SEED, std::strlen(IV_SEED), iv);

    if (crypt_decrypt(iv, encrypted, key, ESP_NOW_KEY_LEN) != 0) {
        LOGGER.error("Failed to decrypt key");
        return false;
    }
    return true;
}

/** Generate a non-zero random sender ID using hardware RNG. */
static uint32_t generateSenderId() {
    uint32_t id;
    do {
        id = esp_random();
    } while (id == 0);
    return id;
}

ChatSettingsData getDefaultSettings() {
    if (defaultSenderId == 0) defaultSenderId = generateSenderId();
    return ChatSettingsData{
        .senderId = defaultSenderId,
        .nickname = "Device",
        .encryptionKey = {},
        .hasEncryptionKey = false,
        .chatChannel = "#general"
    };
}

ChatSettingsData loadSettings() {
    ChatSettingsData settings = getDefaultSettings();

    auto settings_path = getSettingsFilePath();
    if (!file::isFile(settings_path)) {
        settings.senderId = generateSenderId();
        return settings;
    }

    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(settings_path, map)) {
        settings.senderId = generateSenderId();
        return settings;
    }

    auto it = map.find(KEY_SENDER_ID);
    if (it != map.end() && !it->second.empty()) {
        settings.senderId = static_cast<uint32_t>(strtoul(it->second.c_str(), nullptr, 10));
    }
    // Generate sender ID if missing or zero
    if (settings.senderId == 0) {
        settings.senderId = generateSenderId();
    }

    it = map.find(KEY_NICKNAME);
    if (it != map.end() && !it->second.empty()) {
        settings.nickname = it->second.substr(0, MAX_NICKNAME_LEN);
    }

    it = map.find(KEY_ENCRYPTION_KEY);
    if (it != map.end() && !it->second.empty()) {
        if (decryptKey(it->second, settings.encryptionKey.data())) {
            settings.hasEncryptionKey = true;
        }
    }

    it = map.find(KEY_CHAT_CHANNEL);
    if (it != map.end() && !it->second.empty()) {
        settings.chatChannel = it->second.substr(0, MAX_TARGET_LEN);
    }

    return settings;
}

bool saveSettings(const ChatSettingsData& settings) {
    std::map<std::string, std::string> map;

    map[KEY_SENDER_ID] = std::to_string(settings.senderId);
    map[KEY_NICKNAME] = settings.nickname;
    map[KEY_CHAT_CHANNEL] = settings.chatChannel;

    if (settings.hasEncryptionKey) {
        std::string encryptedHex;
        if (!encryptKey(settings.encryptionKey.data(), encryptedHex)) {
            return false;
        }
        map[KEY_ENCRYPTION_KEY] = encryptedHex;
    } else {
        map[KEY_ENCRYPTION_KEY] = "";
    }

    auto settings_path = getSettingsFilePath();
    if (!file::findOrCreateParentDirectory(settings_path, 0755)) {
        LOG_E(TAG, "Failed to create parent dir for %s", settings_path.c_str());
        return false;
    }
    return file::savePropertiesFile(settings_path, map);
}

bool settingsFileExists() {
    return access(getSettingsFilePath().c_str(), F_OK) == 0;
}

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
