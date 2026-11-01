#include <Tactility/app/wifimanage/View.h>
#include <Tactility/app/wifimanage/WifiManagePrivate.h>

#include <Tactility/app/wifiapsettings/WifiApSettings.h>
#include <Tactility/app/wificonnect/WifiConnect.h>

#include <app/event.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <wifi/wifi_autoconnect.h>
#include <wifi/wifi_settings.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/wifi.h>
#include <tactility/log.h>

#include <lvgl/lvgl.h>

#include <atomic>

namespace tt::app::wifimanage {

constexpr auto* TAG = "WifiManage";

extern const ::AppManifest manifest;

namespace {

enum class RadioRequest { None, On, Off };

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

    // Requests from the UI that block, so the app's task handles them. Guarded by the mutex.
    uint32_t actionBit = 0;
    RadioRequest radioRequest = RadioRequest::None;
    bool disconnectRequested = false;
    std::string connectRequest;
    bool connectRequested = false;

    void lock() { mutex.lock(); }
    void unlock() { mutex.unlock(); }
};


static void onConnect(void* userData, const std::string& ssid) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->lock();
    ctx->connectRequest = ssid;
    ctx->connectRequested = true;
    ctx->unlock();
    task_event_group_signal(ctx->eventGroup, ctx->actionBit);
}

static void onShowApSettings(const std::string& ssid) {
    wifiapsettings::start(ssid);
}

static void onDisconnect(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->lock();
    ctx->disconnectRequested = true;
    ctx->unlock();
    task_event_group_signal(ctx->eventGroup, ctx->actionBit);
}

static void onWifiToggled(void* userData, bool enabled) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->lock();
    ctx->radioRequest = enabled ? RadioRequest::On : RadioRequest::Off;
    ctx->unlock();
    task_event_group_signal(ctx->eventGroup, ctx->actionBit);
}

static void onConnectToHidden() {
    wificonnect::start();
}

/** Starting a scan doesn't block, so this runs directly on the LVGL task. */
static bool onRefresh() {
    Device* wifi_device = nullptr;
    if (device_get_first_by_type(&WIFI_TYPE, &wifi_device) != ERROR_NONE) {
        LOG_W(TAG, "No WiFi device found");
        return false;
    }
    bool started = true;
    if (!wifi_is_scanning(wifi_device)) {
        error_t result = wifi_scan(wifi_device);
        if (result != ERROR_NONE) {
            LOG_E(TAG, "Failed to start scan (%s)", error_to_string(result));
            started = false;
        }
    }
    device_put(wifi_device);
    return started;
}

/** Handles the requests of the UI. Turning the radio on blocks, so this runs on the app's task. */
void processRequests(Context* ctx) {
    // Keeps queued requests until the Wi-Fi device is subscribed
    if (ctx->wifiDevice == nullptr) {
        return;
    }

    ctx->lock();
    const auto radio_request = ctx->radioRequest;
    const bool disconnect = ctx->disconnectRequested;
    const bool connect_requested = ctx->connectRequested;
    const std::string ssid = std::move(ctx->connectRequest);
    ctx->connectRequested = false;
    ctx->radioRequest = RadioRequest::None;
    ctx->disconnectRequested = false;
    ctx->connectRequest.clear();
    ctx->unlock();

    if (radio_request != RadioRequest::None) {
        const bool enabled = radio_request == RadioRequest::On;
        const error_t result = enabled ? wifi_set_radio_on(ctx->wifiDevice) : wifi_set_radio_off(ctx->wifiDevice);
        if (result == ERROR_NONE) {
            if (wifi_settings_set_enable(enabled) != ERROR_NONE) {
                LOG_E(TAG, "Failed to save the Wi-Fi state");
            }
        } else {
            LOG_E(TAG, "Failed to set radio state (%s)", error_to_string(result));
        }
    }

    if (disconnect) {
        // Keeps auto-connect from immediately reconnecting
        wifi_autoconnect_pause_until_connected();
        const error_t result = wifi_station_disconnect(ctx->wifiDevice);
        if (result != ERROR_NONE) {
            LOG_E(TAG, "Failed to disconnect (%s)", error_to_string(result));
        }
    }

    if (connect_requested) {
        WifiApSettings ap;
        if (wifi_settings_load(ssid.c_str(), &ap) != ERROR_NONE) {
            LOG_I(TAG, "Starting connection dialog");
            wificonnect::start(ssid);
            return;
        }
        LOG_I(TAG, "Connecting with known credentials");
        error_t result = wifi_set_radio_on(ctx->wifiDevice);
        if (result == ERROR_NONE) {
            if (wifi_settings_set_enable(true) != ERROR_NONE) {
                LOG_E(TAG, "Failed to save the Wi-Fi state");
            }
            result = wifi_station_connect(ctx->wifiDevice, ap.ssid, ap.password, ap.channel);
        }
        if (result != ERROR_NONE) {
            LOG_E(TAG, "Failed to connect (%s)", error_to_string(result));
        }
    }
}

void updateView(Context* ctx, bool rebuildList) {
    // Same lock order as createWidgets() (called with the LVGL lock already held, per the
    // window-manager's WindowCreateWidgetsFn contract, then acquiring ctx->mutex) - acquiring
    // these in the opposite order here would deadlock against a concurrent createWidgets() call.
    lvgl_lock();
    ctx->lock();
    ctx->view.update(rebuildList);
    ctx->unlock();
    lvgl_unlock();
}

void updateStateFromDevice(Context* ctx) {
    WifiRadioState radio_state = WIFI_RADIO_STATE_OFF;
    WifiStationState station_state = WIFI_STATION_STATE_DISCONNECTED;
    char ssid[33] = {};
    wifi_get_radio_state(ctx->wifiDevice, &radio_state);
    wifi_get_station_state(ctx->wifiDevice, &station_state);
    if (station_state != WIFI_STATION_STATE_DISCONNECTED) {
        wifi_station_get_target_ssid(ctx->wifiDevice, ssid);
    }
    ctx->state.setRadioState(radio_state);
    ctx->state.setStationState(station_state);
    ctx->state.setConnectionTarget(ssid);
}

void scanIfIdle(Context* ctx) {
    if (!wifi_is_scanning(ctx->wifiDevice)) {
        error_t result = wifi_scan(ctx->wifiDevice);
        if (result != ERROR_NONE) {
            LOG_I(TAG, "Can't start scan (%s)", error_to_string(result));
            ctx->state.setScanStartFailed();
            updateView(ctx, true);
        }
    }
}

void onWifiEvent(Context* ctx, WifiEvent event) {
    updateStateFromDevice(ctx);
    LOG_I(TAG, "Update with radio state %d, station state %d", (int)ctx->state.getRadioState(), (int)ctx->state.getStationState());
    // Rebuilding the network list resets the keyboard focus, so it only happens for scans that were
    // requested by this app, radio state changes and connection changes.
    bool rebuild_list = false;
    switch (event.type) {
        case WIFI_EVENT_TYPE_SCAN_STARTED:
            ctx->state.setScanning(true);
            break;
        case WIFI_EVENT_TYPE_SCAN_FINISHED:
            ctx->state.setScanning(false);
            if (ctx->state.takeListRefreshRequest()) {
                ctx->state.updateApRecords(ctx->wifiDevice);
                rebuild_list = true;
            }
            break;
        case WIFI_EVENT_TYPE_STATION_STATE_CHANGED:
        case WIFI_EVENT_TYPE_STATION_CONNECTION_RESULT:
            rebuild_list = true;
            break;
        case WIFI_EVENT_TYPE_RADIO_STATE_CHANGED:
            rebuild_list = true;
            if (event.radio_state == WIFI_RADIO_STATE_ON) {
                ctx->state.requestListRefresh();
                scanIfIdle(ctx);
            }
            break;
        default:
            break;
    }

    updateView(ctx, rebuild_list);
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
        .userData = &ctx,
        .onWifiToggled = onWifiToggled,
        .onConnectSsid = onConnect,
        .onDisconnect = onDisconnect,
        .onShowApSettings = onShowApSettings,
        .onConnectToHidden = onConnectToHidden,
        .onRefresh = onRefresh
    };

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    if (task_event_group_claim_bit(&event_group, &ctx.refreshBit) != ERROR_NONE) {
        LOG_W(TAG, "Failed to claim a refresh bit; resurfacing after burial won't repopulate the view");
    }

    if (task_event_group_claim_bit(&event_group, &ctx.actionBit) != ERROR_NONE) {
        LOG_W(TAG, "Failed to claim an action bit; connect and radio requests won't be handled");
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

    // State update (it has its own locking)
    if (ctx.wifiDevice != nullptr) {
        updateStateFromDevice(&ctx);
        ctx.state.setScanning(wifi_is_scanning(ctx.wifiDevice));
    }

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

    if (ctx.wifiDevice != nullptr && ctx.state.getRadioState() == WIFI_RADIO_STATE_ON) {
        scanIfIdle(&ctx);
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
                    updateStateFromDevice(&ctx);
                    ctx.state.setScanning(wifi_is_scanning(ctx.wifiDevice));
                    if (ctx.state.getRadioState() == WIFI_RADIO_STATE_ON) {
                        scanIfIdle(&ctx);
                    }
                    ctx.needsRefresh = true;
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

        processRequests(&ctx);

        if (ctx.wifiDevice != nullptr) {
            WifiEvent wifi_event {};
            while (wifi_event_poll(&ctx.wifiEventSub, &wifi_event) == ERROR_NONE) {
                onWifiEvent(&ctx, wifi_event);
            }
        }

        if (ctx.needsRefresh.exchange(false)) {
            updateView(&ctx, true);
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
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_context_set_parent(&context, callerAppInstanceId);
    app_start_with_context(&context, &instanceId);
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
