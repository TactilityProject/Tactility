#include <format>
#include <string>
#include <set>

#include <tactility/lvgl_module.h>

#include <Tactility/network/HttpdReq.h>
#include <Tactility/app/wifimanage/View.h>
#include <Tactility/app/wifimanage/WifiManagePrivate.h>
#include <Tactility/Logger.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/service/wifi/Wifi.h>
#include <Tactility/service/wifi/WifiSettings.h>
#include <Tactility/Tactility.h>

namespace tt::app::wifimanage {

static const auto LOGGER = Logger("WifiManageView");

std::shared_ptr<WifiManage> optWifiManage();

static uint8_t mapRssiToPercentage(int rssi) {
    auto abs_rssi = std::abs(rssi);
    if (abs_rssi < 30U) {
        abs_rssi = 30U;
    } else if (abs_rssi > 90U) {
        abs_rssi = 90U;
    }

    auto percentage = (float)(90U - abs_rssi) / 60.f * 100.f;
    return (uint8_t)percentage;
}

static void onEnableSwitchChanged(lv_event_t* event) {
    auto* enable_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool is_on = lv_obj_has_state(enable_switch, LV_STATE_CHECKED);

    auto wifi = std::static_pointer_cast<WifiManage>(getCurrentApp());
    auto bindings = wifi->getBindings();

    bindings.onWifiToggled(is_on);
}

static void onEnableOnBootSwitchChanged(lv_event_t* event) {
    auto* enable_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool is_on = lv_obj_has_state(enable_switch, LV_STATE_CHECKED);
    // Dispatch it, so file IO doesn't block the UI
    getMainDispatcher().dispatch([is_on] {
        service::wifi::settings::setEnableOnBoot(is_on);
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
    LOGGER.debug("connect()");
    auto* widget = lv_event_get_current_target_obj(event);
    auto index = reinterpret_cast<size_t>(lv_obj_get_user_data(widget));
    auto* self = static_cast<View*>(lv_event_get_user_data(event));
    auto ap_records = self->state->getApRecords();

    if (index < ap_records.size()) {
        LOGGER.info("Clicked {}/{}", index, ap_records.size() - 1);
        auto& ssid = ap_records[index].ssid;
        LOGGER.info("Clicked AP: {}", ssid);
        std::string connection_target = service::wifi::getConnectionTarget();
        if (connection_target == ssid) {
            self->bindings->onDisconnect();
        } else {
            self->bindings->onConnectSsid(ssid);
        }
    } else {
        LOGGER.warn("Clicked AP: record {}/{} does not exist", index, ap_records.size() - 1);
    }
}

void View::showDetails(lv_event_t* event) {
    LOGGER.debug("showDetails()");
    auto* widget = lv_event_get_current_target_obj(event);
    auto index = reinterpret_cast<size_t>(lv_obj_get_user_data(widget));
    auto* self = static_cast<View*>(lv_event_get_user_data(event));
    auto ap_records = self->state->getApRecords();

    if (index < ap_records.size()) {
        auto& ssid = ap_records[index].ssid;
        LOGGER.info("Clicked AP: {}", ssid);
        self->bindings->onShowApSettings(ssid);
    } else {
        LOGGER.warn("Clicked AP: record {}/{} does not exist", index, ap_records.size() - 1);
    }
}

void View::createSsidListItem(const service::wifi::ApRecord& record, bool isConnecting, size_t index) {
    if (isConnecting) {
        auto* button = lv_list_add_button(networks_list, LV_SYMBOL_WIFI, record.ssid.c_str());
        lv_obj_add_event_cb(button, showDetails, LV_EVENT_SHORT_CLICKED, this);
    } else {
        const std::string auth_info = (record.auth_mode == WIFI_AUTH_OPEN) ? "(open) " : " ";
        const auto percentage = mapRssiToPercentage(record.rssi);
        const auto label = std::format("{} {}{}%", record.ssid, auth_info, percentage);
        auto* button = lv_list_add_button(networks_list, nullptr, label.c_str());
        lv_obj_set_user_data(button, reinterpret_cast<void*>(index));
        if (service::wifi::settings::contains(record.ssid)) {
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

    using enum service::wifi::RadioState;
    switch (state->getRadioState()) {
        case On:
        case ConnectionPending:
        case ConnectionActive:
            lv_obj_remove_flag(connect_to_hidden, LV_OBJ_FLAG_HIDDEN);
            break;

        case OnPending:
        case OffPending:
        case Off:
            lv_obj_add_flag(connect_to_hidden, LV_OBJ_FLAG_HIDDEN);
            break;
    }
}

void View::updateNetworkList() {
    lv_obj_clean(networks_list);

    // Enable on boot

    auto* enable_on_boot_wrapper = lv_obj_create(networks_list);
    lv_obj_set_size(enable_on_boot_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(enable_on_boot_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(enable_on_boot_wrapper, 0, LV_STATE_DEFAULT);

    auto* enable_label = lv_label_create(enable_on_boot_wrapper);
    lv_label_set_text(enable_label, "Enable on boot");
    lv_obj_align(enable_label, LV_ALIGN_LEFT_MID, 0, 0);

    enable_on_boot_switch = lv_switch_create(enable_on_boot_wrapper);
    lv_obj_align(enable_on_boot_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(enable_on_boot_switch, onEnableOnBootSwitchChanged, LV_EVENT_VALUE_CHANGED, bindings);
    lv_obj_add_event_cb(enable_on_boot_wrapper, onEnableOnBootParentClicked, LV_EVENT_SHORT_CLICKED, enable_on_boot_switch);

    if (lvgl_get_ui_density() == LVGL_UI_DENSITY_COMPACT) {
        lv_obj_set_style_pad_ver(enable_on_boot_wrapper, 2, LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_pad_ver(enable_on_boot_wrapper, 8, LV_STATE_DEFAULT);
    }

    updateEnableOnBootToggle();

    switch (state->getRadioState()) {
        using enum service::wifi::RadioState;
        case OnPending:
        case On:
        case ConnectionPending:
        case ConnectionActive: {

            std::string connection_target = service::wifi::getConnectionTarget();

            // Make safe copy
            auto ap_records = state->getApRecords();

            bool is_connected = !connection_target.empty() &&
                state->getRadioState() == ConnectionActive;
            bool added_connected = false;
            if (is_connected && !ap_records.empty()) {
                for (int i = 0; i < ap_records.size(); ++i) {
                    auto& record = ap_records[i];
                    if (record.ssid == connection_target) {
                        lv_list_add_text(networks_list, "Connected");
                        createSsidListItem(record, false, i);
                        added_connected = true;
                        break;
                    }
                }
            }

            lv_list_add_text(networks_list, "Other networks");
            std::set<std::string> used_ssids;
            if (!ap_records.empty()) {
                for (int i = 0; i < ap_records.size(); ++i) {
                    auto& record = ap_records[i];
                    if (!used_ssids.contains(record.ssid)) {
                        bool connection_target_match = (record.ssid == connection_target);
                        bool is_connecting = connection_target_match
                            && state->getRadioState() == ConnectionPending &&
                            !connection_target.empty();
                        bool skip = connection_target_match && added_connected;
                        if (!skip) {
                            createSsidListItem(record, is_connecting, i);
                        }
                        used_ssids.insert(record.ssid);
                    }
                }
                lv_obj_clear_flag(networks_list, LV_OBJ_FLAG_HIDDEN);
            } else if (!state->hasScannedAfterRadioOn() || state->isScanning()) {
                // hasScannedAfterRadioOn() prevents briefly showing "No networks found" when turning radio on.
                lv_obj_add_flag(networks_list, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(networks_list, LV_OBJ_FLAG_HIDDEN);
                lv_obj_t* label = lv_label_create(networks_list);
                lv_label_set_text(label, "No networks found.");
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
    if (state->getRadioState() == service::wifi::RadioState::On && state->isScanning()) {
        lv_obj_remove_flag(scanning_spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(scanning_spinner, LV_OBJ_FLAG_HIDDEN);
    }
}

void View::updateWifiToggle() {
    lv_obj_clear_state(enable_switch, LV_STATE_ANY);
    switch (state->getRadioState()) {
        using enum service::wifi::RadioState;
        case On:
        case ConnectionPending:
        case ConnectionActive:
            lv_obj_add_state(enable_switch, LV_STATE_CHECKED);
            break;
        case OnPending:
            lv_obj_add_state(enable_switch, LV_STATE_CHECKED);
            lv_obj_add_state(enable_switch, LV_STATE_DISABLED);
            break;
        case Off:
            lv_obj_remove_state(enable_switch, LV_STATE_CHECKED);
            lv_obj_remove_state(enable_switch, LV_STATE_DISABLED);
            break;
        case OffPending:
            lv_obj_remove_state(enable_switch, LV_STATE_CHECKED);
            lv_obj_add_state(enable_switch, LV_STATE_DISABLED);
            break;
    }
}

void View::updateEnableOnBootToggle() {
    if (enable_on_boot_switch != nullptr) {
        lv_obj_clear_state(enable_on_boot_switch, LV_STATE_ANY);
        if (service::wifi::settings::shouldEnableOnBoot()) {
            lv_obj_add_state(enable_on_boot_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(enable_on_boot_switch, LV_STATE_CHECKED);
        }
    }
}

// endregion Secondary updates

// region Main

void View::init(const AppContext& app, lv_obj_t* parent) {

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    root = parent;

    paths = app.getPaths();

    // Toolbar

    lv_obj_t* toolbar = lvgl::toolbar_create(parent, app);

    scanning_spinner = lvgl::toolbar_add_spinner_action(toolbar);

    enable_switch = lvgl::toolbar_add_switch_action(toolbar);
    lv_obj_add_event_cb(enable_switch, onEnableSwitchChanged, LV_EVENT_VALUE_CHANGED, bindings);

     // Networks

    networks_list = lv_list_create(parent);
    lv_obj_set_flex_grow(networks_list, 1);
    lv_obj_set_width(networks_list, LV_PCT(100));
}

void View::update() {
    updateWifiToggle();
    updateScanning();
    updateNetworkList();
    updateConnectToHidden();
}

} // namespace
