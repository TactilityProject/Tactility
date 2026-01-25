#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include "ChatState.h"
#include "ChatView.h"
#include "ChatSettings.h"

#include <Tactility/app/App.h>
#include <Tactility/service/espnow/EspNow.h>

namespace tt::app::chat {

class ChatApp final : public App {

    ChatState state;
    ChatView view = ChatView(this, &state);
    service::espnow::ReceiverSubscription receiveSubscription = -1;
    ChatSettingsData settings;
    bool isFirstLaunch = false;

    void onReceive(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length);
    void enableEspNow();
    void disableEspNow();

public:
    void onCreate(AppContext& appContext) override;
    void onDestroy(AppContext& appContext) override;
    void onShow(AppContext& context, lv_obj_t* parent) override;

    void sendMessage(const std::string& text);
    void applySettings(const std::string& nickname, const std::string& keyHex);
    void switchChannel(const std::string& chatChannel);

    const ChatSettingsData& getSettings() const { return settings; }

    ~ChatApp() override = default;
};

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
