#include <Tactility/app/wifimanage/WifiManagePrivate.h>
#include <Tactility/app/wifimanage/View.h>

#include <Tactility/app/AppContext.h>
#include <Tactility/app/wifiapsettings/WifiApSettings.h>
#include <Tactility/app/wificonnect/WifiConnect.h>
#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/loader/Loader.h>

#include <tactility/lvgl_symbols_shared.h>

namespace tt::app::wifimanage {

static const auto LOGGER = Logger("WifiManage");

extern const AppManifest manifest;

static void onConnect(const std::string& ssid) {
    service::wifi::settings::WifiApSettings settings;
    if (service::wifi::settings::load(ssid, settings)) {
        LOGGER.info("Connecting with known credentials");
        service::wifi::connect(settings, false);
    } else {
        LOGGER.info("Starting connection dialog");
        wificonnect::start(ssid);
    }
}

static void onShowApSettings(const std::string& ssid) {
    wifiapsettings::start(ssid);
}

static void onDisconnect() {
    service::wifi::disconnect();
}

static void onWifiToggled(bool enabled) {
    service::wifi::setEnabled(enabled);
}

static void onConnectToHidden() {
    wificonnect::start();
}

WifiManage::WifiManage() {
    bindings = (Bindings) {
        .onWifiToggled = onWifiToggled,
        .onConnectSsid = onConnect,
        .onDisconnect = onDisconnect,
        .onShowApSettings = onShowApSettings,
        .onConnectToHidden = onConnectToHidden
    };
}

void WifiManage::lock() {
    mutex.lock();
}

void WifiManage::unlock() {
    mutex.unlock();
}

void WifiManage::requestViewUpdate() {
    lock();
    if (isViewEnabled) {
        if (lvgl::lock(1000)) {
            view.update();
            lvgl::unlock();
        } else {
            LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "LVGL");
        }
    }
    unlock();
}

void WifiManage::onWifiEvent(service::wifi::WifiEvent event) {
    auto radio_state = service::wifi::getRadioState();
    LOGGER.info("Update with state {}", service::wifi::radioStateToString(radio_state));
    getState().setRadioState(radio_state);
    switch (event) {
        using enum service::wifi::WifiEvent;
        case ScanStarted:
            getState().setScanning(true);
            break;
        case ScanFinished:
            getState().setScanning(false);
            getState().updateApRecords();
            break;
        case RadioStateOn:
            if (!service::wifi::isScanning()) {
                service::wifi::scan();
            }
            break;
        default:
            break;
    }

    requestViewUpdate();
}

void WifiManage::onShow(AppContext& app, lv_obj_t* parent) {
    wifiSubscription = service::wifi::getPubsub()->subscribe([this](auto event) {
        onWifiEvent(event);
    });

    // State update (it has its own locking)
    state.setRadioState(service::wifi::getRadioState());
    state.setScanning(service::wifi::isScanning());
    state.updateApRecords();

    // View update
    lock();
    isViewEnabled = true;
    state.setConnectSsid("Connected"); // TODO update with proper SSID
    view.init(app, parent);
    view.update();
    unlock();

    service::wifi::RadioState radio_state = service::wifi::getRadioState();
    bool can_scan = radio_state == service::wifi::RadioState::On ||
        radio_state == service::wifi::RadioState::ConnectionPending ||
        radio_state == service::wifi::RadioState::ConnectionActive;
    std::string connection_target = service::wifi::getConnectionTarget();
    LOGGER.info("Radio: {}, Scanning: {}, Connected to: {}, Can scan: {}",
        service::wifi::radioStateToString(radio_state), 
        service::wifi::isScanning(),
        connection_target.empty() ? "(none)" : connection_target.c_str(),
        can_scan);
    if (can_scan && !service::wifi::isScanning()) {
        service::wifi::scan();
    }
}

void WifiManage::onHide(AppContext& app) {
    lock();
    service::wifi::getPubsub()->unsubscribe(wifiSubscription);
    wifiSubscription = nullptr;
    isViewEnabled = false;
    unlock();
}

extern const AppManifest manifest = {
    .appId = "WifiManage",
    .appName = "Wi-Fi",
    .appIcon = LVGL_SYMBOL_NETWORK_MANAGE,
    .appCategory = Category::Settings,
    .createApp = create<WifiManage>
};

LaunchId start() {
    return app::start(manifest.appId);
}

} // namespace
