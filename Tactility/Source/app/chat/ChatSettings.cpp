#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/ChatSettings.h>
#include <Tactility/app/chat/ChatProtocol.h>

#include <Tactility/crypt/Crypt.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/Logger.h>

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>

namespace tt::app::chat {

static const auto LOGGER = Logger("ChatSettings");

constexpr auto* KEY_NICKNAME = "nickname";
constexpr auto* KEY_ENCRYPTION_KEY = "encryptionKey";
constexpr auto* KEY_CHAT_CHANNEL = "chatChannel";

// IV_SEED provides basic obfuscation for stored encryption keys, not strong encryption.
// The device master key (from crypt::getIv) provides the actual security.
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
        LOGGER.error("readHex() length mismatch");
        return false;
    }

    char hex[3] = { 0 };
    for (size_t i = 0; i < length; i++) {
        hex[0] = input[i * 2];
        hex[1] = input[i * 2 + 1];
        char* endptr;
        unsigned long val = strtoul(hex, &endptr, 16);
        if (endptr != hex + 2) {
            LOGGER.error("readHex() invalid hex character");
            return false;
        }
        buffer[i] = static_cast<uint8_t>(val);
    }
    return true;
}

static bool encryptKey(const uint8_t key[ESP_NOW_KEY_LEN], std::string& hexOutput) {
    uint8_t iv[16];
    crypt::getIv(IV_SEED, strlen(IV_SEED), iv);

    uint8_t encrypted[ESP_NOW_KEY_LEN];
    if (crypt::encrypt(iv, key, encrypted, ESP_NOW_KEY_LEN) != 0) {
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
    crypt::getIv(IV_SEED, strlen(IV_SEED), iv);

    if (crypt::decrypt(iv, encrypted, key, ESP_NOW_KEY_LEN) != 0) {
        LOGGER.error("Failed to decrypt key");
        return false;
    }
    return true;
}

ChatSettingsData getDefaultSettings() {
    return ChatSettingsData{
        .nickname = "Device",
        .encryptionKey = {},
        .hasEncryptionKey = false,
        .chatChannel = "#general"
    };
}

ChatSettingsData loadSettings() {
    ChatSettingsData settings = getDefaultSettings();

    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(CHAT_SETTINGS_FILE, map)) {
        return settings;
    }

    auto it = map.find(KEY_NICKNAME);
    if (it != map.end() && !it->second.empty()) {
        settings.nickname = it->second.substr(0, SENDER_NAME_SIZE - 1);
    }

    it = map.find(KEY_ENCRYPTION_KEY);
    if (it != map.end() && !it->second.empty()) {
        if (decryptKey(it->second, settings.encryptionKey.data())) {
            settings.hasEncryptionKey = true;
        }
    }

    it = map.find(KEY_CHAT_CHANNEL);
    if (it != map.end() && !it->second.empty()) {
        settings.chatChannel = it->second.substr(0, TARGET_SIZE - 1);
    }

    return settings;
}

bool saveSettings(const ChatSettingsData& settings) {
    std::map<std::string, std::string> map;

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

    return file::savePropertiesFile(CHAT_SETTINGS_FILE, map);
}

bool settingsFileExists() {
    return access(CHAT_SETTINGS_FILE, F_OK) == 0;
}

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
