#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLED)

#include <Tactility/app/chat/ChatAppPrivate.h>
#include <Tactility/app/chat/ChatProtocol.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
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

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.isFirstLaunch = !settingsFileExists();
    ctx.settings = loadSettings();
    ctx.state.setLocalNickname(ctx.settings.nickname);
    if (!ctx.settings.chatChannel.empty()) {
        ctx.state.setCurrentChannel(ctx.settings.chatChannel);
    }
    enableEspNow(&ctx);

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    auto espnow_subscription = service::espnow::subscribeReceiver(
        [&ctx](const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length) {
            onReceive(&ctx, receiveInfo, data, length);
        }
    );

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }
    }

    service::espnow::unsubscribeReceiver(espnow_subscription);
    disableEspNow(&ctx);

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.chat",
    .name = "Chat",
    .category = APP_CATEGORY_USER,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED || CONFIG_ESP_HOSTED_ENABLED
