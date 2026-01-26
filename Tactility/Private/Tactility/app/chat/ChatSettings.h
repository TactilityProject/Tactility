#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <array>
#include <cstdint>
#include <string>

#include <esp_now.h>

namespace tt::app::chat {

constexpr auto* CHAT_SETTINGS_FILE = "/data/settings/chat.properties";

struct ChatSettingsData {
    uint32_t senderId = 0;  // Unique device ID (randomly generated on first launch)
    std::string nickname = "Device";
    std::array<uint8_t, ESP_NOW_KEY_LEN> encryptionKey = {};
    bool hasEncryptionKey = false;
    std::string chatChannel = "#general";
};

ChatSettingsData loadSettings();
bool saveSettings(const ChatSettingsData& settings);
ChatSettingsData getDefaultSettings();
bool settingsFileExists();

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
