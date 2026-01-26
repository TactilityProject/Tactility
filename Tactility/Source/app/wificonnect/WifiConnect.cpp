#include <Tactility/app/wificonnect/WifiConnect.h>

#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/app/AppContext.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/service/wifi/Wifi.h>

namespace tt::app::wificonnect {

static const auto LOGGER = Logger("WifiConnect");
constexpr auto* WIFI_CONNECT_PARAM_SSID = "ssid"; // String
constexpr auto* WIFI_CONNECT_PARAM_PASSWORD = "password"; // String

extern const AppManifest manifest;

static void onConnect(const service::wifi::settings::WifiApSettings& ap_settings, bool remember, void* parameter) {
    auto* wifi = static_cast<WifiConnect*>(parameter);
    wifi->getState().setApSettings(ap_settings);
    wifi->getState().setConnecting(true);
    service::wifi::connect(ap_settings, remember);
}

void WifiConnect::onWifiEvent(service::wifi::WifiEvent event) {
    State& state = getState();
    switch (event) {
        case service::wifi::WifiEvent::ConnectionFailed:
            if (state.isConnecting()) {
                state.setConnecting(false);
                state.setConnectionError(true);
                requestViewUpdate();
            }
            break;
        case service::wifi::WifiEvent::ConnectionSuccess:
            if (getState().isConnecting()) {
                state.setConnecting(false);
                stop(manifest.appId);
            }
            break;
        default:
            break;
    }
    requestViewUpdate();
}

WifiConnect::WifiConnect() {
    wifiSubscription = service::wifi::getPubsub()->subscribe([this](auto event)  {
        onWifiEvent(event);
    });

    bindings = (Bindings) {
        .onConnectSsid = onConnect,
        .onConnectSsidContext = this,
    };
}

WifiConnect::~WifiConnect() {
    service::wifi::getPubsub()->unsubscribe(wifiSubscription);
}

void WifiConnect::lock() {
    mutex.lock();
}

void WifiConnect::unlock() {
    mutex.unlock();
}

void WifiConnect::requestViewUpdate() {
    lock();
    if (viewEnabled) {
        if (lvgl::lock(1000)) {
            view.update();
            lvgl::unlock();
        } else {
            LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "LVGL");
        }
    }
    unlock();
}

void WifiConnect::onShow(AppContext& app, lv_obj_t* parent) {
    lock();
    viewEnabled = true;
    view.init(app, parent);
    view.update();
    unlock();
}

void WifiConnect::onHide(AppContext& app) {
    // No need to lock view, as this is called from within Gui's LVGL context
    lock();
    viewEnabled = false;
    unlock();
}

extern const AppManifest manifest = {
    .appId = "WifiConnect",
    .appName = "Wi-Fi Connect",
    .appIcon = LV_SYMBOL_WIFI,
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<WifiConnect>
};

LaunchId start(const std::string& ssid, const std::string& password) {
    auto parameters = std::make_shared<Bundle>();
    parameters->putString(WIFI_CONNECT_PARAM_SSID, ssid);
    parameters->putString(WIFI_CONNECT_PARAM_PASSWORD, password);
    return app::start(manifest.appId, parameters);
}

bool optSsidParameter(const std::shared_ptr<const Bundle>& bundle, std::string& ssid) {
    return bundle->optString(WIFI_CONNECT_PARAM_SSID, ssid);
}

bool optPasswordParameter(const std::shared_ptr<const Bundle>& bundle, std::string& password) {
    return bundle->optString(WIFI_CONNECT_PARAM_PASSWORD, password);
}

} // namespace
