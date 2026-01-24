#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/app/AppManifest.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/Assets.h>
#include <Tactility/Logger.h>
#include <Tactility/service/espnow/EspNow.h>

#include "Tactility/lvgl/LvglSync.h"

#include <cstdio>
#include <cstring>
#include <esp_wifi.h>
#include <lvgl.h>

namespace tt::app::chat {

static const auto LOGGER = Logger("ChatApp");
constexpr uint8_t BROADCAST_ADDRESS[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

class ChatApp : public App {

    lv_obj_t* msg_list = nullptr;
    lv_obj_t* input_field = nullptr;
    service::espnow::ReceiverSubscription receiveSubscription;

    void addMessage(const char* message) {
        lv_obj_t* msg_label = lv_label_create(msg_list);
        lv_label_set_text(msg_label, message);
        lv_obj_set_width(msg_label, lv_pct(100));
        lv_label_set_long_mode(msg_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(msg_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_pad_all(msg_label, 2, 0);
        lv_obj_scroll_to_y(msg_list, lv_obj_get_scroll_y(msg_list) + 20, LV_ANIM_ON);
    }

    static void onSendClicked(lv_event_t* e) {
        auto* self = static_cast<ChatApp*>(lv_event_get_user_data(e));
        auto* msg = lv_textarea_get_text(self->input_field);
        const auto msg_len = strlen(msg);

        if (self->msg_list && msg && msg_len) {
            self->addMessage(msg);

            if (!service::espnow::send(BROADCAST_ADDRESS, reinterpret_cast<const uint8_t*>(msg), msg_len)) {
                LOGGER.error("Failed to send message");
            }

            lv_textarea_set_text(self->input_field, "");
        }
    }

    void onReceive(const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length) {
        // Append \0 to make it a string
        auto buffer = static_cast<char*>(malloc(length + 1));
        memcpy(buffer, data, length);
        buffer[length] = 0x00;
        const std::string message_prefixed = std::string("Received: ") + buffer;

        lvgl::getSyncLock()->lock();
        addMessage(message_prefixed.c_str());
        lvgl::getSyncLock()->unlock();

        free(buffer);
    }

public:

    void onCreate(AppContext& appContext) override {
        // TODO: Move this to a configuration screen/app
        static const uint8_t key[ESP_NOW_KEY_LEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        auto config = service::espnow::EspNowConfig(
            const_cast<uint8_t*>(key),
            service::espnow::Mode::Station,
            1,
            false,
            false
        );

        service::espnow::enable(config);

        receiveSubscription = service::espnow::subscribeReceiver([this](const esp_now_recv_info_t* receiveInfo, const uint8_t* data, int length) {
            onReceive(receiveInfo, data, length);
        });
    }

    void onDestroy(AppContext& appContext) override {
        service::espnow::unsubscribeReceiver(receiveSubscription);

        if (service::espnow::isEnabled()) {
            service::espnow::disable();
        }
    }

    void onShow(AppContext& context, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lvgl::toolbar_create(parent, context);

        // Message list
        msg_list = lv_list_create(parent);
        lv_obj_set_flex_grow(msg_list, 1);
        lv_obj_set_width(msg_list, LV_PCT(100));
        lv_obj_set_flex_grow(msg_list, 1);
        lv_obj_set_style_bg_color(msg_list, lv_color_hex(0x262626), 0);
        lv_obj_set_style_border_width(msg_list, 1, 0);
        lv_obj_set_style_pad_ver(msg_list, 0, 0);
        lv_obj_set_style_pad_hor(msg_list, 4, 0);

        // Input panel
        auto* bottom_wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(bottom_wrapper, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(bottom_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(bottom_wrapper, 0, 0);
        lv_obj_set_style_pad_column(bottom_wrapper, 4, 0);
        lv_obj_set_style_border_opa(bottom_wrapper, 0, LV_STATE_DEFAULT);

        // Input field
        input_field = lv_textarea_create(bottom_wrapper);
        lv_obj_set_flex_grow(input_field, 1);
        lv_textarea_set_placeholder_text(input_field, "Type a message...");
        lv_textarea_set_one_line(input_field, true);

        // Send button
        auto* send_btn = lv_button_create(bottom_wrapper);
        lv_obj_set_style_margin_all(send_btn, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_margin_top(send_btn, 2, LV_STATE_DEFAULT); // Hack to fix alignment
        lv_obj_add_event_cb(send_btn, onSendClicked, LV_EVENT_CLICKED, this);

        auto* btn_label = lv_label_create(send_btn);
        lv_label_set_text(btn_label, "Send");
        lv_obj_center(btn_label);
    }

    ~ChatApp() override = default;
};

extern const AppManifest manifest = {
    .appId = "Chat",
    .appName = "Chat",
    .appIcon = TT_ASSETS_APP_ICON_CHAT,
    .createApp = create<ChatApp>
};

}

#endif
