#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/ChatAppPrivate.h>
#include <Tactility/app/chat/ChatProtocol.h>

#include <Tactility/app/AppManifest.h>
#include <Tactility/Assets.h>
#include <Tactility/Logger.h>
#include <Tactility/lvgl/LvglSync.h>

#include <algorithm>
#include <cstdlib>
#include <cctype>

namespace tt::app::chat {

static const auto LOGGER = Logger("ChatApp");
static constexpr uint8_t BROADCAST_ADDRESS[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

void ChatApp::enableEspNow() {
    static uint8_t defaultKey[ESP_NOW_KEY_LEN] = {};
    auto config = service::espnow::EspNowConfig(
        settings.hasEncryptionKey ? settings.encryptionKey.data() : defaultKey,
        service::espnow::Mode::Station,
        1, // Channel 1 default; actual channel determined by WiFi if connected
        false,
        settings.hasEncryptionKey
    );
    service::espnow::enable(config);
}

void ChatApp::disableEspNow() {
    if (service::espnow::isEnabled()) {
        service::espnow::disable();
    }
}

void ChatApp::onCreate(AppContext& appContext) {
    isFirstLaunch = !settingsFileExists();
    settings = loadSettings();
    state.setLocalNickname(settings.nickname);
    if (!settings.chatChannel.empty()) {
        state.setCurrentChannel(settings.chatChannel);
    }
    enableEspNow();

    receiveSubscription = service::espnow::subscribeReceiver(
        [this](const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length) {
            onReceive(receiveInfo, data, length);
        }
    );
}

void ChatApp::onDestroy(AppContext& appContext) {
    service::espnow::unsubscribeReceiver(receiveSubscription);
    disableEspNow();
}

void ChatApp::onShow(AppContext& context, lv_obj_t* parent) {
    view.init(context, parent);
    if (isFirstLaunch) {
        view.showSettings(settings);
    }
}

void ChatApp::onReceive(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length) {
    ParsedMessage parsed;
    if (!deserializeMessage(data, length, parsed)) {
        return;
    }

    StoredMessage msg;
    msg.displayText = parsed.senderName + ": " + parsed.message;
    msg.target = parsed.target;
    msg.isOwn = false;

    state.addMessage(msg);

    {
        auto lock = lvgl::getSyncLock()->asScopedLock();
        lock.lock();
        view.displayMessage(msg);
    }
}

void ChatApp::sendMessage(const std::string& text) {
    if (text.empty()) return;

    std::string nickname = state.getLocalNickname();
    std::string channel = state.getCurrentChannel();

    Message wireMsg;
    size_t packetSize = serializeMessage(nickname, channel, text, wireMsg);
    if (packetSize == 0) {
        LOGGER.error("Failed to serialize message");
        return;
    }

    if (!service::espnow::send(BROADCAST_ADDRESS, reinterpret_cast<const uint8_t*>(&wireMsg), packetSize)) {
        LOGGER.error("Failed to send message");
        return;
    }

    StoredMessage msg;
    msg.displayText = nickname + ": " + text;
    msg.target = channel;
    msg.isOwn = true;

    state.addMessage(msg);

    {
        auto lock = lvgl::getSyncLock()->asScopedLock();
        lock.lock();
        view.displayMessage(msg);
    }
}

void ChatApp::applySettings(const std::string& nickname, const std::string& keyHex) {
    bool needRestart = false;

    // Trim nickname to protocol limit
    settings.nickname = nickname.substr(0, SENDER_NAME_SIZE - 1);

    // Parse hex key
    if (keyHex.size() == ESP_NOW_KEY_LEN * 2) {
        bool validHex = std::all_of(keyHex.begin(), keyHex.end(), [](unsigned char c) { return std::isxdigit(c); });
        if (validHex) {
            uint8_t newKey[ESP_NOW_KEY_LEN];
            for (int i = 0; i < ESP_NOW_KEY_LEN; i++) {
                char hex[3] = { keyHex[i * 2], keyHex[i * 2 + 1], 0 };
                newKey[i] = static_cast<uint8_t>(strtoul(hex, nullptr, 16));
            }
            // Restart if key changed OR if encryption is being enabled
            bool wasEnabled = settings.hasEncryptionKey;
            if (!wasEnabled || !std::equal(newKey, newKey + ESP_NOW_KEY_LEN, settings.encryptionKey.begin())) {
                std::copy(newKey, newKey + ESP_NOW_KEY_LEN, settings.encryptionKey.begin());
                needRestart = true;
            }
            settings.hasEncryptionKey = true;
        } else {
            LOGGER.warn("Invalid hex characters in encryption key");
        }
    } else if (keyHex.empty()) {
        if (settings.hasEncryptionKey) {
            settings.encryptionKey.fill(0);
            settings.hasEncryptionKey = false;
            needRestart = true;
        }
    } else {
        LOGGER.warn("Key must be exactly {} hex characters, got {}", ESP_NOW_KEY_LEN * 2, keyHex.size());
    }

    state.setLocalNickname(settings.nickname);
    saveSettings(settings);

    if (needRestart) {
        disableEspNow();
        enableEspNow();
    }
}

void ChatApp::switchChannel(const std::string& chatChannel) {
    const auto trimmedChannel = chatChannel.substr(0, TARGET_SIZE - 1);
    state.setCurrentChannel(trimmedChannel);
    settings.chatChannel = trimmedChannel;
    saveSettings(settings);

    {
        auto lock = lvgl::getSyncLock()->asScopedLock();
        lock.lock();
        view.refreshMessageList();
    }
}

extern const AppManifest manifest = {
    .appId = "Chat",
    .appName = "Chat",
    .appIcon = TT_ASSETS_APP_ICON_CHAT,
    .createApp = create<ChatApp>
};

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
