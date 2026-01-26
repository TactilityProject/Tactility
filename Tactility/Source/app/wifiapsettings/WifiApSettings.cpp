#include "Tactility/lvgl/LvglSync.h"

#include <Tactility/service/wifi/WifiApSettings.h>
#include <Tactility/service/wifi/Wifi.h>
#include <Tactility/app/App.h>
#include <Tactility/app/AppContext.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/Check.h>
#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/lvgl/Toolbar.h>

#include <lvgl.h>

namespace tt::app::wifiapsettings {

static const auto LOGGER = Logger("WifiApSettings");

extern const AppManifest manifest;

void start(const std::string& ssid) {
    auto bundle = std::make_shared<Bundle>();
    bundle->putString("ssid", ssid);
    app::start(manifest.appId, bundle);
}

class WifiApSettings : public App {

    bool viewEnabled = false;
    lv_obj_t* busySpinner = nullptr;
    lv_obj_t* connectButton = nullptr;
    lv_obj_t* disconnectButton = nullptr;
    std::string ssid;
    PubSub<service::wifi::WifiEvent>::SubscriptionHandle wifiSubscription = nullptr;

    static void onPressForget(lv_event_t* event) {
        std::vector<std::string> choices = {
            "Yes",
            "No"
        };
        alertdialog::start("Confirmation", "Forget the Wi-Fi access point?", choices);
    }

    static void onToggleAutoConnect(lv_event_t* event) {
        auto* self = static_cast<WifiApSettings*>(lv_event_get_user_data(event));
        auto* enable_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
        bool is_on = lv_obj_has_state(enable_switch, LV_STATE_CHECKED);

        service::wifi::settings::WifiApSettings settings;
        if (service::wifi::settings::load(self->ssid.c_str(), settings)) {
            settings.autoConnect = is_on;
            if (!service::wifi::settings::save(settings)) {
                LOGGER.error("Failed to save settings");
            }
        } else {
            LOGGER.error("Failed to load settings");
        }
    }

    static void onPressConnect(lv_event_t* event) {
        auto app = getCurrentAppContext();
        auto parameters = app->getParameters();
        check(parameters != nullptr, "Parameters missing");

        std::string ssid = parameters->getString("ssid");
        service::wifi::settings::WifiApSettings settings;
        if (service::wifi::settings::load(ssid.c_str(), settings)) {
            auto* button = lv_event_get_target_obj(event);
            lv_obj_add_state(button, LV_STATE_DISABLED);
            service::wifi::connect(settings, false);
        }
    }

    static void onPressDisconnect(lv_event_t* event) {
        if (service::wifi::getRadioState() == service::wifi::RadioState::ConnectionActive) {
            auto* button = lv_event_get_target_obj(event);
            lv_obj_add_state(button, LV_STATE_DISABLED);
            service::wifi::disconnect();
        }
    }

    void onWifiEvent(service::wifi::WifiEvent event) const {
        requestViewUpdate();
    }

    void requestViewUpdate() const {
        if (viewEnabled) {
            if (lvgl::lock(1000)) {
                updateViews();
                lvgl::unlock();
            } else {
                LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "LVGL");
            }
        }
    }

    void updateConnectButton() const {
        if (service::wifi::getConnectionTarget() == ssid && service::wifi::getRadioState() == service::wifi::RadioState::ConnectionActive) {
            lv_obj_remove_flag(disconnectButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(connectButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_state(disconnectButton, LV_STATE_DISABLED);
        } else {
            lv_obj_add_flag(disconnectButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(connectButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_state(connectButton, LV_STATE_DISABLED);
        }
    }

    void updateBusySpinner() const {
        if (service::wifi::getRadioState() == service::wifi::RadioState::ConnectionPending) {
            lv_obj_remove_flag(busySpinner, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(busySpinner, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void updateViews() const {
        updateConnectButton();
        updateBusySpinner();
    }

public:

    void onCreate(AppContext& app) override {
        const auto parameters = app.getParameters();
        check(parameters != nullptr, "Parameters missing");
        ssid = parameters->getString("ssid");
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        wifiSubscription = service::wifi::getPubsub()->subscribe([this](auto event) {
            requestViewUpdate();
        });

        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        auto* toolbar = lvgl::toolbar_create(parent, ssid);
        busySpinner = lvgl::toolbar_add_spinner_action(toolbar);

        auto* wrapper = lv_obj_create(parent);
        lv_obj_set_width(wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(wrapper, 1);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
        lvgl::obj_set_style_bg_invisible(wrapper);

        disconnectButton = lv_button_create(wrapper);
        lv_obj_set_width(disconnectButton, LV_PCT(100));
        lv_obj_add_event_cb(disconnectButton, onPressDisconnect, LV_EVENT_SHORT_CLICKED, nullptr);
        auto* disconnect_label = lv_label_create(disconnectButton);
        lv_obj_align(disconnect_label, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(disconnect_label, "Disconnect");

        connectButton = lv_button_create(wrapper);
        lv_obj_set_width(connectButton, LV_PCT(100));
        lv_obj_add_event_cb(connectButton, onPressConnect, LV_EVENT_SHORT_CLICKED, nullptr);
        auto* connect_label = lv_label_create(connectButton);
        lv_obj_align(connect_label, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(connect_label, "Connect");

        // Forget

        auto* forget_button = lv_button_create(wrapper);
        lv_obj_set_width(forget_button, LV_PCT(100));
        lv_obj_add_event_cb(forget_button, onPressForget, LV_EVENT_SHORT_CLICKED, nullptr);
        auto* forget_button_label = lv_label_create(forget_button);
        lv_obj_align(forget_button_label, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(forget_button_label, "Forget");

        // Auto-connect

        auto* auto_connect_wrapper = lv_obj_create(wrapper);
        lv_obj_set_size(auto_connect_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lvgl::obj_set_style_bg_invisible(auto_connect_wrapper);
        lv_obj_set_style_pad_all(auto_connect_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(auto_connect_wrapper, 0, LV_STATE_DEFAULT);

        auto* auto_connect_label = lv_label_create(auto_connect_wrapper);
        lv_label_set_text(auto_connect_label, "Auto-connect");
        lv_obj_align(auto_connect_label, LV_ALIGN_LEFT_MID, 0, 0);

        auto* auto_connect_switch = lv_switch_create(auto_connect_wrapper);
        lv_obj_add_event_cb(auto_connect_switch, onToggleAutoConnect, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_align(auto_connect_switch, LV_ALIGN_RIGHT_MID, 0, 0);

        service::wifi::settings::WifiApSettings settings;
        if (service::wifi::settings::load(ssid.c_str(), settings)) {
            if (settings.autoConnect) {
                lv_obj_add_state(auto_connect_switch, LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(auto_connect_switch, LV_STATE_CHECKED);
            }
        } else {
            LOGGER.warn("No settings found");
            lv_obj_add_flag(forget_button, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(auto_connect_wrapper, LV_OBJ_FLAG_HIDDEN);
        }

        viewEnabled = true;

        updateViews();
    }

    void onHide(AppContext& app) override {
        service::wifi::getPubsub()->unsubscribe(wifiSubscription);
        wifiSubscription = nullptr;
        viewEnabled = false;
    }

    void onResult(AppContext& appContext, LaunchId launchId, Result result, std::unique_ptr<Bundle> bundle) override {
        if (result != Result::Ok || bundle == nullptr) {
            return;
        }

        auto index = alertdialog::getResultIndex(*bundle);
        if (index != 0) { // 0 = Yes
            return;
        }

        auto parameters = appContext.getParameters();
        check(parameters != nullptr, "Parameters missing");

        std::string ssid = parameters->getString("ssid");
        if (!service::wifi::settings::remove(ssid.c_str())) {
            LOGGER.error("Failed to remove SSID");
            return;
        }

        LOGGER.info("Removed SSID");
        if (
            service::wifi::getRadioState() == service::wifi::RadioState::ConnectionActive &&
            service::wifi::getConnectionTarget() == ssid
        ) {
            service::wifi::disconnect();
        }

        // Stop app
        stop();
    }
};

extern const AppManifest manifest = {
    .appId = "WifiApSettings",
    .appName = "Wi-Fi AP Settings",
    .appIcon = LV_SYMBOL_WIFI,
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<WifiApSettings>
};

} // namespace

