#include <Tactility/app/wifimanage/View.h>
#include <Tactility/app/wifimanage/WifiManagePrivate.h>

#include <Tactility/app/wifiapsettings/WifiApSettings.h>
#include <Tactility/app/wificonnect/WifiConnect.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/log.h>

#include <lvgl/lvgl.h>

namespace tt::app::wifimanage {

constexpr auto* TAG = "WifiManage";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    PubSub<service::wifi::WifiEvent>::SubscriptionHandle wifiSubscription = nullptr;
    Mutex mutex;
    Bindings bindings {};
    State state;
    View view = View(&bindings, &state);

    void lock() { mutex.lock(); }
    void unlock() { mutex.unlock(); }
};


static void onConnect(const std::string& ssid) {
    service::wifi::settings::WifiApSettings settings;
    if (service::wifi::settings::load(ssid, settings)) {
        LOG_I(TAG, "Connecting with known credentials");
        service::wifi::connect(settings, false);
    } else {
        LOG_I(TAG, "Starting connection dialog");
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

void requestViewUpdate(Context* ctx) {
    ctx->lock();
    lvgl_lock();
    ctx->view.update();
    lvgl_unlock();
    ctx->unlock();
}

void onWifiEvent(Context* ctx, service::wifi::WifiEvent event) {
    auto radio_state = service::wifi::getRadioState();
    LOG_I(TAG, "Update with state %s", service::wifi::radioStateToString(radio_state));
    ctx->state.setRadioState(radio_state);
    switch (event.type) {
        case WIFI_EVENT_TYPE_SCAN_STARTED:
            ctx->state.setScanning(true);
            break;
        case WIFI_EVENT_TYPE_SCAN_FINISHED:
            ctx->state.setScanning(false);
            ctx->state.updateApRecords();
            break;
        case WIFI_EVENT_TYPE_RADIO_STATE_CHANGED:
            if (event.radio_state == WIFI_RADIO_STATE_ON && !service::wifi::isScanning()) {
                service::wifi::scan();
            }
            break;
        default:
            break;
    }

    requestViewUpdate(ctx);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->lock();
    ctx->state.setConnectSsid("Connected"); // TODO update with proper SSID
    ctx->view.init(ctx->appInstanceId, parent);
    ctx->view.update();
    ctx->unlock();
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx;
    ctx.appInstanceId = appInstanceId;
    ctx.bindings = (Bindings) {
        .onWifiToggled = onWifiToggled,
        .onConnectSsid = onConnect,
        .onDisconnect = onDisconnect,
        .onShowApSettings = onShowApSettings,
        .onConnectToHidden = onConnectToHidden
    };

    ctx.wifiSubscription = service::wifi::getPubsub()->subscribe([&ctx](auto event) {
        onWifiEvent(&ctx, event);
    });

    // State update (it has its own locking)
    ctx.state.setRadioState(service::wifi::getRadioState());
    ctx.state.setScanning(service::wifi::isScanning());
    ctx.state.updateApRecords();

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    service::wifi::RadioState radio_state = service::wifi::getRadioState();
    bool can_scan = radio_state == service::wifi::RadioState::On ||
        radio_state == service::wifi::RadioState::ConnectionPending ||
        radio_state == service::wifi::RadioState::ConnectionActive;
    std::string connection_target = service::wifi::getConnectionTarget();
    LOG_I(TAG, "Radio: %s, Scanning: %d, Connected to: %s, Can scan: %d",
        service::wifi::radioStateToString(radio_state),
        (int)service::wifi::isScanning(),
        connection_target.empty() ? "(none)" : connection_target.c_str(),
        (int)can_scan);
    if (can_scan && !service::wifi::isScanning()) {
        service::wifi::scan();
    }

    bool shouldClose = false;
    while (!shouldClose) {
        AppEvent event {};
        if (app_event_await(&sub, &event, portMAX_DELAY) != ERROR_NONE) {
            break;
        }
        switch (event.type) {
            case APP_EVENT_CLOSE:
                app_manager_finish(appInstanceId);
                shouldClose = true;
                break;
            default:
                break;
        }
    }

    ctx.lock();
    service::wifi::getPubsub()->unsubscribe(ctx.wifiSubscription);
    ctx.wifiSubscription = nullptr;
    ctx.unlock();

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

uint32_t start(uint32_t callerAppInstanceId) {
    uint32_t instanceId = 0;
    app_manager_start_for_result(manifest.id, callerAppInstanceId, 0, nullptr, &instanceId);
    return instanceId;
}

extern const ::AppManifest manifest = {
    .id = "WifiManage",
    .name = "Wi-Fi",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace tt::app::wifimanage
