#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/chat/ChatView.h>
#include <Tactility/app/chat/ChatAppPrivate.h>
#include <Tactility/app/chat/ChatProtocol.h>

#include <Tactility/lvgl/Toolbar.h>

#include <cstdio>
#include <cstring>

namespace tt::app::chat {

void ChatView::addMessageToList(lv_obj_t* list, const StoredMessage& msg) {
    auto* label = lv_label_create(list);
    lv_label_set_text(label, msg.displayText.c_str());
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_pad_all(label, 2, 0);

    if (msg.isOwn) {
        lv_obj_set_style_text_color(label, lv_color_hex(0x80C0FF), 0);
    }
}

void ChatView::updateToolbarTitle() {
    if (!state || !toolbar) return;
    std::string channel = state->getCurrentChannel();
    std::string title = "Chat: " + channel;
    lvgl::toolbar_set_title(toolbar, title);
}

void ChatView::createInputBar(lv_obj_t* parent) {
    inputWrapper = lv_obj_create(parent);
    auto* wrapper = inputWrapper;
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(wrapper, 0, 0);
    lv_obj_set_style_pad_column(wrapper, 4, 0);
    lv_obj_set_style_border_opa(wrapper, 0, LV_STATE_DEFAULT);

    inputField = lv_textarea_create(wrapper);
    lv_obj_set_flex_grow(inputField, 1);
    lv_textarea_set_placeholder_text(inputField, "Type a message...");
    lv_textarea_set_one_line(inputField, true);
    lv_textarea_set_max_length(inputField, MAX_MESSAGE_LEN);

    auto* sendBtn = lv_button_create(wrapper);
    lv_obj_set_style_margin_all(sendBtn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_margin_top(sendBtn, 2, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(sendBtn, onSendClicked, LV_EVENT_CLICKED, this);

    auto* btnLabel = lv_label_create(sendBtn);
    lv_label_set_text(btnLabel, "Send");
    lv_obj_center(btnLabel);
}

void ChatView::createSettingsPanel(lv_obj_t* parent) {
    settingsPanel = lv_obj_create(parent);
    lv_obj_set_width(settingsPanel, LV_PCT(100));
    lv_obj_set_flex_grow(settingsPanel, 1);
    lv_obj_set_flex_flow(settingsPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(settingsPanel, 8, 0);
    lv_obj_set_style_pad_row(settingsPanel, 6, 0);
    lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);

    // Nickname
    auto* nickLabel = lv_label_create(settingsPanel);
    lv_label_set_text(nickLabel, "Nickname (max 23):");

    nicknameInput = lv_textarea_create(settingsPanel);
    lv_obj_set_width(nicknameInput, LV_PCT(100));
    lv_textarea_set_one_line(nicknameInput, true);
    lv_textarea_set_max_length(nicknameInput, MAX_NICKNAME_LEN);

    // Encryption key
    auto* keyLabel = lv_label_create(settingsPanel);
    lv_label_set_text(keyLabel, "Key (32 hex chars):");

    keyInput = lv_textarea_create(settingsPanel);
    lv_obj_set_width(keyInput, LV_PCT(100));
    lv_textarea_set_one_line(keyInput, true);
    lv_textarea_set_max_length(keyInput, ESP_NOW_KEY_LEN * 2);
    lv_textarea_set_placeholder_text(keyInput, "empty = all zeros");

    // Buttons
    auto* btnRow = lv_obj_create(settingsPanel);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(btnRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 8, 0);
    lv_obj_set_style_border_opa(btnRow, 0, 0);

    auto* saveBtn = lv_button_create(btnRow);
    lv_obj_add_event_cb(saveBtn, onSettingsSave, LV_EVENT_CLICKED, this);
    auto* saveLbl = lv_label_create(saveBtn);
    lv_label_set_text(saveLbl, "Save");

    auto* cancelBtn = lv_button_create(btnRow);
    lv_obj_add_event_cb(cancelBtn, onSettingsCancel, LV_EVENT_CLICKED, this);
    auto* cancelLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLbl, "Cancel");
}

void ChatView::createChannelPanel(lv_obj_t* parent) {
    channelPanel = lv_obj_create(parent);
    lv_obj_set_width(channelPanel, LV_PCT(100));
    lv_obj_set_flex_grow(channelPanel, 1);
    lv_obj_set_flex_flow(channelPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(channelPanel, 8, 0);
    lv_obj_set_style_pad_row(channelPanel, 6, 0);
    lv_obj_add_flag(channelPanel, LV_OBJ_FLAG_HIDDEN);

    auto* label = lv_label_create(channelPanel);
    lv_label_set_text(label, "Channel (e.g. #general):");

    channelInput = lv_textarea_create(channelPanel);
    lv_obj_set_width(channelInput, LV_PCT(100));
    lv_textarea_set_one_line(channelInput, true);
    lv_textarea_set_max_length(channelInput, MAX_TARGET_LEN);

    auto* btnRow = lv_obj_create(channelPanel);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(btnRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(btnRow, 0, 0);
    lv_obj_set_style_pad_column(btnRow, 8, 0);
    lv_obj_set_style_border_opa(btnRow, 0, 0);

    auto* okBtn = lv_button_create(btnRow);
    lv_obj_add_event_cb(okBtn, onChannelSave, LV_EVENT_CLICKED, this);
    auto* okLbl = lv_label_create(okBtn);
    lv_label_set_text(okLbl, "OK");

    auto* cancelBtn = lv_button_create(btnRow);
    lv_obj_add_event_cb(cancelBtn, onChannelCancel, LV_EVENT_CLICKED, this);
    auto* cancelLbl = lv_label_create(cancelBtn);
    lv_label_set_text(cancelLbl, "Cancel");
}

void ChatView::init(AppContext& appContext, lv_obj_t* parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    toolbar = lvgl::toolbar_create(parent, appContext);
    lvgl::toolbar_add_text_button_action(toolbar, LV_SYMBOL_LIST, onChannelClicked, this);
    lvgl::toolbar_add_text_button_action(toolbar, LV_SYMBOL_SETTINGS, onSettingsClicked, this);
    updateToolbarTitle();

    // Message list
    msgList = lv_list_create(parent);
    lv_obj_set_flex_grow(msgList, 1);
    lv_obj_set_width(msgList, LV_PCT(100));
    lv_obj_set_style_bg_color(msgList, lv_color_hex(0x262626), 0);
    lv_obj_set_style_border_width(msgList, 0, 0);
    lv_obj_set_style_pad_ver(msgList, 2, 0);
    lv_obj_set_style_pad_hor(msgList, 4, 0);

    // Input bar
    createInputBar(parent);

    // Overlay panels (hidden by default)
    createSettingsPanel(parent);
    createChannelPanel(parent);
}

void ChatView::displayMessage(const StoredMessage& msg) {
    if (!msgList || !state) return;

    // Only show if matches current channel or broadcast
    std::string channel = state->getCurrentChannel();
    if (!msg.target.empty() && msg.target != channel) {
        return;
    }
    addMessageToList(msgList, msg);
    lv_obj_scroll_to_y(msgList, LV_COORD_MAX, LV_ANIM_ON);
}

void ChatView::refreshMessageList() {
    if (!msgList || !state) return;

    lv_obj_clean(msgList);
    auto filtered = state->getFilteredMessages();
    for (const auto& msg : filtered) {
        addMessageToList(msgList, msg);
    }
    lv_obj_scroll_to_y(msgList, LV_COORD_MAX, LV_ANIM_OFF);
    updateToolbarTitle();
}

void ChatView::showSettings(const ChatSettingsData& current) {
    if (!settingsPanel) return;

    lv_textarea_set_text(nicknameInput, current.nickname.c_str());

    if (current.hasEncryptionKey) {
        char hexStr[ESP_NOW_KEY_LEN * 2 + 1] = {};
        for (size_t i = 0; i < ESP_NOW_KEY_LEN; i++) {
            snprintf(hexStr + i * 2, 3, "%02x", current.encryptionKey[i]);
        }
        lv_textarea_set_text(keyInput, hexStr);
    } else {
        lv_textarea_set_text(keyInput, "");
    }

    lv_obj_add_flag(msgList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(inputWrapper, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);
}

void ChatView::hideSettings() {
    if (!settingsPanel || !msgList || !inputWrapper) return;
    lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(msgList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(inputWrapper, LV_OBJ_FLAG_HIDDEN);
}

void ChatView::showChannelSelector() {
    if (!channelPanel || !state) return;

    std::string current = state->getCurrentChannel();
    lv_textarea_set_text(channelInput, current.c_str());

    lv_obj_add_flag(msgList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(inputWrapper, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(channelPanel, LV_OBJ_FLAG_HIDDEN);
}

void ChatView::hideChannelSelector() {
    if (!channelPanel || !msgList || !inputWrapper) return;
    lv_obj_add_flag(channelPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(msgList, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(inputWrapper, LV_OBJ_FLAG_HIDDEN);
}

void ChatView::onSendClicked(lv_event_t* e) {
    auto* self = static_cast<ChatView*>(lv_event_get_user_data(e));
    auto* text = lv_textarea_get_text(self->inputField);
    if (text && strlen(text) > 0) {
        self->app->sendMessage(std::string(text));
        lv_textarea_set_text(self->inputField, "");
    }
}

void ChatView::onSettingsClicked(lv_event_t* e) {
    auto* self = static_cast<ChatView*>(lv_event_get_user_data(e));
    self->showSettings(self->app->getSettings());
}

void ChatView::onSettingsSave(lv_event_t* e) {
    auto* self = static_cast<ChatView*>(lv_event_get_user_data(e));

    auto* nickname = lv_textarea_get_text(self->nicknameInput);
    auto* keyHex = lv_textarea_get_text(self->keyInput);

    if (nickname && strlen(nickname) > 0) {
        self->app->applySettings(
            std::string(nickname),
            keyHex ? std::string(keyHex) : std::string()
        );
    }
    self->hideSettings();
}

void ChatView::onSettingsCancel(lv_event_t* e) {
    auto* self = static_cast<ChatView*>(lv_event_get_user_data(e));
    self->hideSettings();
}

void ChatView::onChannelClicked(lv_event_t* e) {
    auto* self = static_cast<ChatView*>(lv_event_get_user_data(e));
    self->showChannelSelector();
}

void ChatView::onChannelSave(lv_event_t* e) {
    auto* self = static_cast<ChatView*>(lv_event_get_user_data(e));
    auto* text = lv_textarea_get_text(self->channelInput);
    if (text && strlen(text) > 0) {
        self->app->switchChannel(std::string(text));
    }
    self->hideChannelSelector();
}

void ChatView::onChannelCancel(lv_event_t* e) {
    auto* self = static_cast<ChatView*>(lv_event_get_user_data(e));
    self->hideChannelSelector();
}

} // namespace tt::app::chat

#endif // CONFIG_SOC_WIFI_SUPPORTED && !CONFIG_SLAVE_SOC_WIFI_SUPPORTED
