#include <format>
#include <string>
#include <set>

#include <Tactility/network/HttpdReq.h>
#include <Tactility/app/wifimanage/View.h>
#include <Tactility/app/wifimanage/WifiManagePrivate.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/Tactility.h>

#include <app/event.h>
#include <lvgl/widgets/list.h>
#include <lvgl/widgets/toolbar.h>

#include <tactility/log.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>

#include <wifi/wifi_settings.h>

namespace tt::app::wifimanage {

constexpr auto* TAG = "WifiManageView";

static void onBackPressed(lv_event_t* event) {
    auto* appInstanceId = static_cast<uint32_t*>(lv_event_get_user_data(event));
    app_event_emit_close(*appInstanceId);
}

static uint8_t mapRssiToPercentage(int rssi) {
    auto abs_rssi = std::abs(rssi);
    if (abs_rssi < 30U) {
        abs_rssi = 30U;
    } else if (abs_rssi > 90U) {
        abs_rssi = 90U;
    }

    auto percentage = (float)(90U - abs_rssi) / 60.f * 100.f;
    return static_cast<uint8_t>(percentage);
}

static void onEnableSwitchChanged(lv_event_t* event) {
    auto* enable_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool is_on = lv_obj_has_state(enable_switch, LV_STATE_CHECKED);
    auto* bindings = static_cast<Bindings*>(lv_event_get_user_data(event));
    bindings->onWifiToggled(is_on);
}

static void onEnableOnBootSwitchChanged(lv_event_t* event) {
    auto* enable_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool is_on = lv_obj_has_state(enable_switch, LV_STATE_CHECKED);
    // Dispatch it, so file IO doesn't block the UI
    getMainDispatcher().dispatch([is_on] {
        wifi_settings_set_enable_on_boot(is_on);
    });
}

static void onEnableOnBootParentClicked(lv_event_t* event) {
    auto* enable_switch = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
    if (lv_obj_has_state(enable_switch, LV_STATE_CHECKED)) {
        lv_obj_remove_state(enable_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_add_state(enable_switch, LV_STATE_CHECKED);
    }
}

static void onConnectToHiddenClicked(lv_event_t* event) {
    auto* bindings = (Bindings*)lv_event_get_user_data(event);
    bindings->onConnectToHidden();
}

// region Secondary updates

void View::connect(lv_event_t* event) {
    LOG_D(TAG, "connect()");
    auto* widget = lv_event_get_current_target_obj(event);
    auto index = reinterpret_cast<size_t>(lv_obj_get_user_data(widget));
    auto* self = static_cast<View*>(lv_event_get_user_data(event));
    auto ap_records = self->state->getApRecords();

    if (index < ap_records.size()) {
        LOG_I(TAG, "Clicked %zu/%zu", index, ap_records.size() - 1);
        std::string ssid = ap_records[index].ssid;
        LOG_I(TAG, "Clicked AP: %s", ssid.c_str());
        std::string connection_target = self->state->getConnectionTarget();
        if (connection_target == ssid) {
            self->bindings->onDisconnect();
        } else {
            self->bindings->onConnectSsid(ssid);
        }
    } else {
        LOG_W(TAG, "Clicked AP: record %zu/%zu does not exist", index, ap_records.size() - 1);
    }
}

void View::showDetails(lv_event_t* event) {
    LOG_D(TAG, "showDetails()");
    auto* widget = lv_event_get_current_target_obj(event);
    auto index = reinterpret_cast<size_t>(lv_obj_get_user_data(widget));
    auto* self = static_cast<View*>(lv_event_get_user_data(event));
    auto ap_records = self->state->getApRecords();

    if (index < ap_records.size()) {
        std::string ssid = ap_records[index].ssid;
        LOG_I(TAG, "Clicked AP: %s", ssid.c_str());
        self->bindings->onShowApSettings(ssid);
    } else {
        LOG_W(TAG, "Clicked AP: record %zu/%zu does not exist", index, ap_records.size() - 1);
    }
}

void View::onRefreshPressed(lv_event_t* event) {
    auto* self = static_cast<View*>(lv_event_get_user_data(event));
    self->state->requestListRefresh();
    self->bindings->onRefresh();
}

/** Creates a title with a card below it, and returns the list in the card */
static lv_obj_t* createSection(lv_obj_t* parent, const char* title) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, title);

    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 0, LV_STATE_DEFAULT);
    // The list items' pressed and focused backgrounds follow the card's rounded corners
    lv_obj_set_style_clip_corner(card, true, LV_STATE_DEFAULT);

    // The card provides the background
    auto* list = lvgl_list_create(card);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(list, 0, LV_STATE_DEFAULT);
    return list;
}

void View::createSsidListItem(lv_obj_t* list, const WifiApRecord& record, bool isConnecting, size_t index) {
    if (isConnecting) {
        auto* button = lvgl_list_add_button(list, LV_SYMBOL_WIFI, record.ssid);
        lv_obj_add_event_cb(button, showDetails, LV_EVENT_SHORT_CLICKED, this);
    } else {
        const std::string auth_info = (record.authentication_type == WIFI_AUTHENTICATION_TYPE_OPEN) ? "(open) " : " ";
        const auto percentage = mapRssiToPercentage(record.rssi);
        const auto label = std::format("{} {}{}%", std::string(record.ssid), auth_info, percentage);
        auto* button = lvgl_list_add_button(list, nullptr, label.c_str());
        lv_obj_set_user_data(button, reinterpret_cast<void*>(index));
        if (wifi_settings_contains(record.ssid)) {
            lv_obj_add_event_cb(button, showDetails, LV_EVENT_SHORT_CLICKED, this);
        } else {
            lv_obj_add_event_cb(button, connect, LV_EVENT_SHORT_CLICKED, this);
        }
    }
}

void View::updateConnectToHidden() {
    if (connect_to_hidden == nullptr) {
        return;
    }

    if (state->getRadioState() == WIFI_RADIO_STATE_ON) {
        lv_obj_set_hidden(connect_to_hidden, false);
    } else {
        lv_obj_set_hidden(connect_to_hidden, true);
    }
}

void View::updateNetworkList() {
    lv_obj_clean(networks_list);

    // Enable on boot

    auto* enable_on_boot_wrapper = lv_obj_create(networks_list);
    lv_obj_set_size(enable_on_boot_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(enable_on_boot_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(enable_on_boot_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(enable_on_boot_wrapper, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(enable_on_boot_wrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(enable_on_boot_wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(enable_on_boot_wrapper, false);

    auto* enable_label = lv_label_create(enable_on_boot_wrapper);
    lv_label_set_text(enable_label, "Enable on boot");
    lv_obj_set_flex_grow(enable_label, 1);

    enable_on_boot_switch = lv_switch_create(enable_on_boot_wrapper);
    lv_obj_add_event_cb(enable_on_boot_switch, onEnableOnBootSwitchChanged, LV_EVENT_VALUE_CHANGED, bindings);
    lv_obj_add_event_cb(enable_on_boot_wrapper, onEnableOnBootParentClicked, LV_EVENT_SHORT_CLICKED, enable_on_boot_switch);

    updateEnableOnBootToggle();

    switch (state->getRadioState()) {
        case WIFI_RADIO_STATE_ON_PENDING:
        case WIFI_RADIO_STATE_ON: {

            std::string connection_target = state->getConnectionTarget();
            auto station_state = state->getStationState();

            // Make safe copy
            auto ap_records = state->getApRecords();

            bool is_connected = !connection_target.empty() &&
                station_state == WIFI_STATION_STATE_CONNECTED;
            bool added_connected = false;
            if (is_connected && !ap_records.empty()) {
                for (int i = 0; i < ap_records.size(); ++i) {
                    auto& record = ap_records[i];
                    if (record.ssid == connection_target) {
                        createSsidListItem(createSection(networks_list, "Connected"), record, false, i);
                        added_connected = true;
                        break;
                    }
                }
            }

            auto* networks_section = createSection(networks_list, "Networks");
            std::set<std::string> used_ssids;
            if (!ap_records.empty()) {
                for (int i = 0; i < ap_records.size(); ++i) {
                    auto& record = ap_records[i];
                    if (!used_ssids.contains(record.ssid)) {
                        bool connection_target_match = (record.ssid == connection_target);
                        bool is_connecting = connection_target_match
                            && station_state == WIFI_STATION_STATE_CONNECTION_PENDING &&
                            !connection_target.empty();
                        bool skip = connection_target_match && added_connected;
                        if (!skip) {
                            createSsidListItem(networks_section, record, is_connecting, i);
                        }
                        used_ssids.insert(record.ssid);
                    }
                }
                lv_obj_set_hidden(networks_list, false);
            } else if (!state->hasScannedAfterRadioOn() || state->isScanning()) {
                // hasScannedAfterRadioOn() prevents briefly showing "No networks found" when turning radio on.
                lv_obj_set_hidden(networks_list, true);
            } else {
                lv_obj_set_hidden(networks_list, false);
                lvgl_list_add_text(networks_section, "No networks found.");
            }

            connect_to_hidden = lv_button_create(networks_list);
            lv_obj_set_width(connect_to_hidden, LV_PCT(100));
            lv_obj_set_style_margin_ver(connect_to_hidden, 4, LV_STATE_DEFAULT);
            auto* connect_to_hidden_label = lv_label_create(connect_to_hidden);
            lv_label_set_text(connect_to_hidden_label, "Connect to hidden SSID");
            lv_obj_add_event_cb(connect_to_hidden, onConnectToHiddenClicked, LV_EVENT_SHORT_CLICKED, bindings);
            break;
        }

        default:
            connect_to_hidden = nullptr;
            // Nothing to do
            break;
    }

}

void View::updateScanning() {
    if (state->getRadioState() == WIFI_RADIO_STATE_ON && state->getStationState() == WIFI_STATION_STATE_DISCONNECTED && state->isScanning()) {
        lv_obj_set_hidden(scanning_spinner, false);
    } else {
        lv_obj_set_hidden(scanning_spinner, true);
    }
}

void View::updateRefreshButton() {
    lv_obj_set_hidden(refresh_button, state->getRadioState() != WIFI_RADIO_STATE_ON);
}

void View::updateWifiToggle() {
    lv_obj_clear_state(enable_switch, LV_STATE_ANY);
    switch (state->getRadioState()) {
        case WIFI_RADIO_STATE_ON:
            lv_obj_add_state(enable_switch, LV_STATE_CHECKED);
            break;
        case WIFI_RADIO_STATE_ON_PENDING:
            lv_obj_add_state(enable_switch, LV_STATE_CHECKED);
            lv_obj_add_state(enable_switch, LV_STATE_DISABLED);
            break;
        case WIFI_RADIO_STATE_OFF:
            lv_obj_remove_state(enable_switch, LV_STATE_CHECKED);
            lv_obj_remove_state(enable_switch, LV_STATE_DISABLED);
            break;
        case WIFI_RADIO_STATE_OFF_PENDING:
            lv_obj_remove_state(enable_switch, LV_STATE_CHECKED);
            lv_obj_add_state(enable_switch, LV_STATE_DISABLED);
            break;
    }
}

void View::updateEnableOnBootToggle() {
    if (enable_on_boot_switch != nullptr) {
        lv_obj_clear_state(enable_on_boot_switch, LV_STATE_ANY);
        if (wifi_settings_get_enable_on_boot()) {
            lv_obj_add_state(enable_on_boot_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(enable_on_boot_switch, LV_STATE_CHECKED);
        }
    }
}

// endregion Secondary updates

// region Main

void View::init(uint32_t newAppInstanceId, lv_obj_t* parent) {
    appInstanceId = newAppInstanceId;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    root = parent;

    // Toolbar

    lv_obj_t* toolbar = lvgl_toolbar_create(parent, "Wi-Fi");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, &appInstanceId);

    scanning_spinner = lvgl_toolbar_add_spinner_action(toolbar);

    refresh_button = lvgl_toolbar_add_image_button_action(toolbar, LV_SYMBOL_REFRESH, onRefreshPressed, this);

    enable_switch = lvgl_toolbar_add_switch_action(toolbar);
    lv_obj_add_event_cb(enable_switch, onEnableSwitchChanged, LV_EVENT_VALUE_CHANGED, bindings);

     // Networks

    networks_list = lv_obj_create(parent);
    lv_obj_set_flex_grow(networks_list, 1);
    lv_obj_set_width(networks_list, LV_PCT(100));
    lv_obj_set_flex_flow(networks_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(networks_list, 0, LV_STATE_DEFAULT);
}

void View::update(bool rebuildList) {
    if (root == nullptr) {
        // Buried (or not yet built) - see reset().
        return;
    }
    updateWifiToggle();
    updateScanning();
    updateRefreshButton();
    if (rebuildList) {
        updateNetworkList();
    }
    updateConnectToHidden();
}

void View::reset() {
    root = nullptr;
    enable_switch = nullptr;
    enable_on_boot_switch = nullptr;
    scanning_spinner = nullptr;
    refresh_button = nullptr;
    networks_list = nullptr;
    connect_to_hidden = nullptr;
}

} // namespace
