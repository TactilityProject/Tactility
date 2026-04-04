#include <Tactility/app/btpeersettings/BtPeerSettings.h>

#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/app/App.h>
#include <Tactility/app/AppContext.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/bluetooth/Bluetooth.h>
#include <Tactility/bluetooth/BluetoothPairedDevice.h>
#include <tactility/check.h>
#include <tactility/drivers/bluetooth.h>

#include <lvgl.h>

namespace tt::app::btpeersettings {

static const auto LOGGER = Logger("BtPeerSettings");

extern const AppManifest manifest;

void start(const std::string& addrHex) {
    auto bundle = std::make_shared<Bundle>();
    bundle->putString("addr", addrHex);
    app::start(manifest.appId, bundle);
}

class BtPeerSettings : public App {

    bool viewEnabled = false;
    lv_obj_t* connectButton = nullptr;
    lv_obj_t* disconnectButton = nullptr;
    std::string addrHex;
    std::array<uint8_t, 6> addr = {};
    int profileId = BT_PROFILE_HID_HOST;
    bool isCurrentlyConnected() const {
        for (const auto& p : bluetooth::getPairedPeers()) {
            if (p.addr == addr) return p.connected;
        }
        return false;
    }

    static void onPressConnect(lv_event_t* event) {
        auto* self = static_cast<BtPeerSettings*>(lv_event_get_user_data(event));
        if (self->profileId == BT_PROFILE_HID_HOST) {
            bluetooth::hidHostConnect(self->addr);
        } else {
            bluetooth::connect(self->addr, self->profileId);
        }
        lv_obj_add_state(lv_event_get_target_obj(event), LV_STATE_DISABLED);
    }

    static void onPressDisconnect(lv_event_t* event) {
        auto* self = static_cast<BtPeerSettings*>(lv_event_get_user_data(event));
        if (self->profileId == BT_PROFILE_HID_HOST) {
            bluetooth::hidHostDisconnect();
        } else {
            bluetooth::disconnect(self->addr, self->profileId);
        }
        lv_obj_add_state(lv_event_get_target_obj(event), LV_STATE_DISABLED);
    }

    static void onPressForget(lv_event_t* event) {
        std::vector<std::string> choices = { "Yes", "No" };
        alertdialog::start("Confirmation", "Forget this device?", choices);
    }

    static void onToggleAutoConnect(lv_event_t* event) {
        auto* self = static_cast<BtPeerSettings*>(lv_event_get_user_data(event));
        bool is_on = lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED);
        bluetooth::settings::PairedDevice device;
        if (bluetooth::settings::load(self->addrHex, device)) {
            device.autoConnect = is_on;
            if (!bluetooth::settings::save(device)) {
                LOGGER.error("Failed to save auto-connect setting");
            }
        }
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

    void updateViews() const {
        if (isCurrentlyConnected()) {
            lv_obj_remove_flag(disconnectButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(connectButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_state(disconnectButton, LV_STATE_DISABLED);
        } else {
            lv_obj_add_flag(disconnectButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(connectButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_state(connectButton, LV_STATE_DISABLED);
        }
    }

public:

    void onCreate(AppContext& app) override {
        const auto parameters = app.getParameters();
        check(parameters != nullptr, "Parameters missing");
        addrHex = parameters->getString("addr");

        // Load addr and profileId from stored settings — avoids manual hex parsing
        // (std::stoul throws on invalid input and exceptions are disabled).
        bluetooth::settings::PairedDevice device;
        if (bluetooth::settings::load(addrHex, device)) {
            addr      = device.addr;
            profileId = device.profileId;
        }
    }

    static void onKernelBtEvent(struct Device* /*device*/, void* context, struct BtEvent /*event*/) {
        static_cast<BtPeerSettings*>(context)->requestViewUpdate();
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        if (struct Device* dev = bluetooth::findFirstDevice()) {
            bluetooth_add_event_callback(dev, this, onKernelBtEvent);
        }

        // Load stored settings (name, autoConnect)
        bluetooth::settings::PairedDevice device;
        bool deviceLoaded = bluetooth::settings::load(addrHex, device);
        std::string title = (deviceLoaded && !device.name.empty()) ? device.name : addrHex;

        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lvgl::toolbar_create(parent, title);

        auto* wrapper = lv_obj_create(parent);
        lv_obj_set_width(wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(wrapper, 1);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
        lvgl::obj_set_style_bg_invisible(wrapper);

        connectButton = lv_button_create(wrapper);
        lv_obj_set_width(connectButton, LV_PCT(100));
        lv_obj_add_event_cb(connectButton, onPressConnect, LV_EVENT_SHORT_CLICKED, this);
        auto* connect_label = lv_label_create(connectButton);
        lv_obj_align(connect_label, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(connect_label, "Connect");

        disconnectButton = lv_button_create(wrapper);
        lv_obj_set_width(disconnectButton, LV_PCT(100));
        lv_obj_add_event_cb(disconnectButton, onPressDisconnect, LV_EVENT_SHORT_CLICKED, this);
        auto* disconnect_label = lv_label_create(disconnectButton);
        lv_obj_align(disconnect_label, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(disconnect_label, "Disconnect");

        auto* forget_button = lv_button_create(wrapper);
        lv_obj_set_width(forget_button, LV_PCT(100));
        lv_obj_add_event_cb(forget_button, onPressForget, LV_EVENT_SHORT_CLICKED, this);
        auto* forget_label = lv_label_create(forget_button);
        lv_obj_align(forget_label, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(forget_label, "Forget");

        // Auto-connect toggle row
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

        if (deviceLoaded && device.autoConnect) {
            lv_obj_add_state(auto_connect_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(auto_connect_switch, LV_STATE_CHECKED);
        }

        viewEnabled = true;
        updateViews();
    }

    void onHide(AppContext& app) override {
        if (struct Device* dev = bluetooth::findFirstDevice()) {
            bluetooth_remove_event_callback(dev, onKernelBtEvent);
        }
        viewEnabled = false;
    }

    void onResult(AppContext& appContext, LaunchId /*launchId*/, Result result, std::unique_ptr<Bundle> bundle) override {
        if (result != Result::Ok || bundle == nullptr) return;
        if (alertdialog::getResultIndex(*bundle) != 0) return; // 0 = Yes

        // Disconnect first if connected
        if (isCurrentlyConnected()) {
            if (profileId == BT_PROFILE_HID_HOST) {
                bluetooth::hidHostDisconnect();
            } else {
                bluetooth::disconnect(addr, profileId);
            }
        }

        bluetooth::unpair(addr);
        stop();
    }
};

extern const AppManifest manifest = {
    .appId = "BtPeerSettings",
    .appName = "BT Device Settings",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<BtPeerSettings>
};

} // namespace tt::app::btpeersettings
