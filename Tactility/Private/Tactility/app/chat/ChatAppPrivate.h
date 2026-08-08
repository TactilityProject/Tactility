#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include "ChatState.h"
#include "ChatView.h"
#include "ChatSettings.h"

#include <Tactility/service/espnow/EspNow.h>

#include <cstdint>
#include <string>

namespace tt::app::chat {

// Replaces the old ChatApp (tt::app::App subclass) under the thread-per-app model. Declared
// here (rather than local to ChatApp.cpp's anonymous namespace, as most converted apps do)
// because ChatView - a separate translation unit - also needs to reference it.
struct Context {
    uint32_t appInstanceId;
    ChatState state;
    ChatView view = ChatView(this, &state);
    service::espnow::ReceiverSubscription receiveSubscription = -1;
    ChatSettingsData settings;
    bool isFirstLaunch = false;
};

void enableEspNow(Context* ctx);
void disableEspNow(Context* ctx);

void sendMessage(Context* ctx, const std::string& text);
void applySettings(Context* ctx, const std::string& nickname, const std::string& keyHex);
void switchChannel(Context* ctx, const std::string& chatChannel);

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED || CONFIG_SLAVE_SOC_WIFI_SUPPORTED
