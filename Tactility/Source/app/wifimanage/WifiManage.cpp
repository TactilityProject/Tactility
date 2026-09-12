#include <Tactility/app/wifimanage/View.h>
#include <Tactility/app/wifimanage/WifiManagePrivate.h>

#include <Tactility/app/wifiapsettings/WifiApSettings.h>
#include <Tactility/app/wificonnect/WifiConnect.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/log.h>

#include <lvgl/lvgl.h>

#include <atomic>

namespace tt::app::wifimanage {

constexpr auto* TAG = "WifiManage";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    // Set once in appMain() before subscribing, left null if this device has no WiFi driver
    Device* wifiDevice = nullptr;
    WifiEventSubscription wifiEventSub {};
    Mutex mutex;
    Bindings bindings {};
    State state;
    View view = View(&bindings, &state);

    TaskEventGroup* eventGroup = nullptr;
    uint32_t refreshBit = 0;
    std::atomic<bool> needsRefresh {false};

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

void updateView(Context* ctx) {
    // Same lock order as createWidgets() (called with the LVGL lock already held, per the
    // window-manager's WindowCreateWidgetsFn contract, then acquiring ctx->mutex) - acquiring
    // these in the opposite order here would deadlock against a concurrent createWidgets() call.
    lvgl_lock();
    ctx->lock();
    ctx->view.update();
    ctx->unlock();
    lvgl_unlock();
}

void onWifiEvent(Context* ctx, WifiEvent event) {
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

    updateView(ctx);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->lock();
    ctx->state.setConnectSsid("Connected"); // TODO update with proper SSID
    ctx->view.init(ctx->appInstanceId, parent);
    ctx->unlock();
    ctx->needsRefresh = true;
    task_event_group_signal(ctx->eventGroup, ctx->refreshBit);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->view.reset();
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx;
    ctx.appInstanceId = appInstanceId;
    ctx.bindings = (Bindings) {
        .onWifiToggled = onWifiToggled,
        .onConnectSsid = onConnect,
        .onDisconnect = onDisconnect,
        .onShowApSettings = onShowApSettings,
        .onConnectToHidden = onConnectToHidden
    };

    // State update (it has its own locking)
    ctx.state.setRadioState(service::wifi::getRadioState());
    ctx.state.setScanning(service::wifi::isScanning());
    ctx.state.updateApRecords();

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    if (task_event_group_claim_bit(&event_group, &ctx.refreshBit) != ERROR_NONE) {
        LOG_W(TAG, "Failed to claim a refresh bit; resurfacing after burial won't repopulate the view");
    }

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    Device* wifi_device = nullptr;
    if (device_get_first_by_type(&WIFI_TYPE, &wifi_device) == ERROR_NONE) {
        if (wifi_event_subscribe(wifi_device, &ctx.wifiEventSub, &event_group) == ERROR_NONE) {
            ctx.wifiDevice = wifi_device;
        } else {
            LOG_W(TAG, "Failed to subscribe to WiFi events");
            device_put(wifi_device);
        }
    } else {
        LOG_W(TAG, "No WiFi device found");
    }

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

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
        // If the wifi device wasn't started yet when this app opened (boot-order race),
        // ctx.wifiDevice is still null and there's no wifi-driven wake source to learn
        // "it's ready now" from, so poll for it on a bounded timeout instead of blocking
        // indefinitely. Once subscribed, this reverts to
        // portMAX_DELAY - task_event_group_wait_any() still returns immediately for app_event
        // and (once live) wifi_event, this timeout only matters while neither has fired yet.
        TickType_t wait_timeout = (ctx.wifiDevice == nullptr) ? pdMS_TO_TICKS(500) : portMAX_DELAY;
        task_event_group_wait_any(&event_group, nullptr, wait_timeout);

        if (ctx.wifiDevice == nullptr) {
            Device* retry_device = nullptr;
            if (device_get_first_by_type(&WIFI_TYPE, &retry_device) == ERROR_NONE) {
                if (wifi_event_subscribe(retry_device, &ctx.wifiEventSub, &event_group) == ERROR_NONE) {
                    ctx.wifiDevice = retry_device;
                } else {
                    device_put(retry_device);
                }
            }
        }

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }

        if (ctx.wifiDevice != nullptr) {
            WifiEvent wifi_event {};
            while (wifi_event_poll(&ctx.wifiEventSub, &wifi_event) == ERROR_NONE) {
                onWifiEvent(&ctx, wifi_event);
            }
        }

        if (ctx.needsRefresh.exchange(false)) {
            updateView(&ctx);
        }
    }

    if (ctx.wifiDevice != nullptr) {
        wifi_event_unsubscribe(ctx.wifiDevice, &ctx.wifiEventSub);
        device_put(ctx.wifiDevice);
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

uint32_t start(uint32_t callerAppInstanceId) {
    uint32_t instanceId = 0;
    app_start_for_result(manifest.id, 0, nullptr, callerAppInstanceId, &instanceId);
    return instanceId;
}

extern const ::AppManifest manifest = {
    .id = "tactility.wifimanage",
    .name = "Wi-Fi",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::wifimanage
