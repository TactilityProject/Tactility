#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/ChatAppPrivate.h>
#include <Tactility/app/chat/ChatProtocol.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/log.h>

#include <lvgl/lvgl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace tt::app::chat {

extern const ::AppManifest manifest;

constexpr auto* TAG = "ChatApp";
static constexpr uint8_t BROADCAST_ADDRESS[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

void enableEspNow(Context* ctx) {
    static uint8_t defaultKey[ESP_NOW_KEY_LEN] = {};
    auto config = service::espnow::EspNowConfig(
        ctx->settings.hasEncryptionKey ? ctx->settings.encryptionKey.data() : defaultKey,
        service::espnow::Mode::Station,
        1, // Channel 1 default; actual channel determined by WiFi if connected
        false,
        ctx->settings.hasEncryptionKey
    );
    service::espnow::enable(config);
}

void disableEspNow(Context* ctx) {
    (void)ctx;
    if (service::espnow::isEnabled()) {
        service::espnow::disable();
    }
}

namespace {


void onReceive(Context* ctx, const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length) {
    if (length <= 0) return;

    ParsedMessage parsed;
    if (!deserializeMessage(data, static_cast<size_t>(length), parsed)) {
        return;
    }

    StoredMessage msg;
    msg.displayText = parsed.senderName + ": " + parsed.message;
    msg.target = parsed.target;
    msg.isOwn = false;

    ctx->state.addMessage(msg);

    lvgl_lock();
    ctx->view.displayMessage(msg);
    lvgl_unlock();
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->view.init(parent);
    if (ctx->isFirstLaunch) {
        ctx->view.showSettings(ctx->settings);
    }
}

} // namespace

void sendMessage(Context* ctx, const std::string& text) {
    if (text.empty()) return;

    std::string nickname = ctx->state.getLocalNickname();
    std::string channel = ctx->state.getCurrentChannel();

    std::vector<uint8_t> wireMsg;
    if (!serializeTextMessage(ctx->settings.senderId, BROADCAST_ID, nickname, channel, text, wireMsg)) {
        LOG_E(TAG, "Failed to serialize message");
        return;
    }

    if (!service::espnow::send(BROADCAST_ADDRESS, wireMsg.data(), wireMsg.size())) {
        LOG_E(TAG, "Failed to send message");
        return;
    }

    StoredMessage msg;
    msg.displayText = nickname + ": " + text;
    msg.target = channel;
    msg.isOwn = true;

    ctx->state.addMessage(msg);

    lvgl_lock();
    ctx->view.displayMessage(msg);
    lvgl_unlock();
}

void applySettings(Context* ctx, const std::string& nickname, const std::string& keyHex) {
    bool needRestart = false;

    // Trim nickname to protocol limit
    ctx->settings.nickname = nickname.substr(0, MAX_NICKNAME_LEN);

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
            bool wasEnabled = ctx->settings.hasEncryptionKey;
            if (!wasEnabled || !std::equal(newKey, newKey + ESP_NOW_KEY_LEN, ctx->settings.encryptionKey.begin())) {
                std::copy(newKey, newKey + ESP_NOW_KEY_LEN, ctx->settings.encryptionKey.begin());
                needRestart = true;
            }
            ctx->settings.hasEncryptionKey = true;
        } else {
            LOG_W(TAG, "Invalid hex characters in encryption key");
        }
    } else if (keyHex.empty()) {
        if (ctx->settings.hasEncryptionKey) {
            ctx->settings.encryptionKey.fill(0);
            ctx->settings.hasEncryptionKey = false;
            needRestart = true;
        }
    } else {
        LOG_W(TAG, "Key must be exactly %d hex characters, got %d", (int)(ESP_NOW_KEY_LEN * 2), (int)keyHex.size());
    }

    ctx->state.setLocalNickname(ctx->settings.nickname);
    saveSettings(ctx->settings);

    if (needRestart) {
        disableEspNow(ctx);
        enableEspNow(ctx);
    }
}

void switchChannel(Context* ctx, const std::string& chatChannel) {
    const auto trimmedChannel = chatChannel.substr(0, MAX_TARGET_LEN);
    ctx->state.setCurrentChannel(trimmedChannel);
    ctx->settings.chatChannel = trimmedChannel;
    saveSettings(ctx->settings);

    lvgl_lock();
    ctx->view.refreshMessageList();
    lvgl_unlock();
}

namespace {

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.isFirstLaunch = !settingsFileExists();
    ctx.settings = loadSettings();
    ctx.state.setLocalNickname(ctx.settings.nickname);
    if (!ctx.settings.chatChannel.empty()) {
        ctx.state.setCurrentChannel(ctx.settings.chatChannel);
    }
    enableEspNow(&ctx);

    ctx.receiveSubscription = service::espnow::subscribeReceiver(
        [&ctx](const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length) {
            onReceive(&ctx, receiveInfo, data, length);
        }
    );


    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        AppEvent event {};
        if (app_event_await(&sub, &event, portMAX_DELAY) != ERROR_NONE) {
            break;
        }
        switch (event.type) {
            case APP_EVENT_CLOSE:
                app_manager_finish(appInstanceId);
                shouldClose = true;
                break;
            default:
                break;
        }
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    service::espnow::unsubscribeReceiver(ctx.receiveSubscription);
    disableEspNow(&ctx);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "Chat",
    .name = "Chat",
    .category = APP_CATEGORY_USER,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED || CONFIG_SLAVE_SOC_WIFI_SUPPORTED
