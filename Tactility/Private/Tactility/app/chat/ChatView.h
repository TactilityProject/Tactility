#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include "ChatState.h"
#include "ChatSettings.h"

#include <Tactility/app/AppContext.h>

#include <esp_now.h>
#include <lvgl.h>

namespace tt::app::chat {

class ChatApp;

class ChatView {

    ChatApp* app;
    ChatState* state;

    lv_obj_t* toolbar = nullptr;
    lv_obj_t* msgList = nullptr;
    lv_obj_t* inputWrapper = nullptr;
    lv_obj_t* inputField = nullptr;

    // Settings panel widgets
    lv_obj_t* settingsPanel = nullptr;
    lv_obj_t* nicknameInput = nullptr;
    lv_obj_t* keyInput = nullptr;

    // Channel selector panel widgets
    lv_obj_t* channelPanel = nullptr;
    lv_obj_t* channelInput = nullptr;

    void createInputBar(lv_obj_t* parent);
    void createSettingsPanel(lv_obj_t* parent);
    void createChannelPanel(lv_obj_t* parent);

    void updateToolbarTitle();

    static void addMessageToList(lv_obj_t* msgList, const StoredMessage& msg);

    static void onSendClicked(lv_event_t* e);
    static void onSettingsClicked(lv_event_t* e);
    static void onSettingsSave(lv_event_t* e);
    static void onSettingsCancel(lv_event_t* e);
    static void onChannelClicked(lv_event_t* e);
    static void onChannelSave(lv_event_t* e);
    static void onChannelCancel(lv_event_t* e);

public:
    ChatView(ChatApp* app, ChatState* state) : app(app), state(state) {}
    ~ChatView() = default;

    ChatView(const ChatView&) = delete;
    ChatView& operator=(const ChatView&) = delete;
    ChatView(ChatView&&) = delete;
    ChatView& operator=(ChatView&&) = delete;

    void init(AppContext& appContext, lv_obj_t* parent);

    void displayMessage(const StoredMessage& msg);
    void refreshMessageList();

    void showSettings(const ChatSettingsData& current);
    void hideSettings();

    void showChannelSelector();
    void hideChannelSelector();
};

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
