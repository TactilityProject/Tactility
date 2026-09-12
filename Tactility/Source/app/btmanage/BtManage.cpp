#include <lvgl/lvgl.h>

#include <Tactility/app/btmanage/BtManagePrivate.h>
#include <Tactility/app/btmanage/View.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/log.h>

namespace tt::app::btmanage {

constexpr auto* TAG = "BtManage";

extern const ::AppManifest manifest;


static void onBtToggled(void* /*context*/, bool requestOn) {
#if defined(CONFIG_BT_NIMBLE_ENABLED)
    Device* dev;
    if (device_get_first_by_type(&BLUETOOTH_TYPE, &dev) == ERROR_NONE) {
        bool radio_on = bluetooth::isRadioOnOrPending(dev);
        if (requestOn && !radio_on) {
            LOG_I(TAG, "Turning on");
            bluetooth::start(dev);
        } else if (!requestOn && radio_on) {
            LOG_I(TAG, "Turning off");
            bluetooth::stop(dev);
        }
        device_put(dev);
    } else {
        LOG_W(TAG, "Toggle: No bluetooth device found");
    }

#endif
}

static void onScanToggled(void* /*context*/, bool enabled) {
    Device* dev;
    if (device_get_first_active_by_type(&BLUETOOTH_TYPE, &dev) != ERROR_NONE) {
        LOG_W(TAG, "Scan: No bluetooth device found");
        return;
    }

    if (enabled) {
        bluetooth_scan_start(dev);
    } else {
        bluetooth_scan_stop(dev);
    }

    device_put(dev);
}

static void onConnectPeer(const std::array<uint8_t, 6>& addr, int profileId) {
    bluetooth::connect(addr, profileId);
}

static void onDisconnectPeer(const std::array<uint8_t, 6>& addr, int profileId) {
    bluetooth::disconnect(addr, profileId);
}

static void onPairPeer(void* /*context*/, const std::array<uint8_t, 6>& addr) {
    // Clicking an unrecognised scan result initiates a HID host connection.
    // Bond exchange happens automatically during the first connection.
    bluetooth::hidHostConnect(addr);
}

static void onForgetPeer(const std::array<uint8_t, 6>& addr) {
    bluetooth::unpair(addr);
}

void requestViewUpdate(Context* ctx) {
    // Lock order must match appMain()'s setup/teardown: both run under the LVGL lock
    // and then take `ctx->mutex` internally. Taking `mutex` before lvgl_lock() here would
    // invert that order and deadlock against a concurrent teardown (GUI task holding the
    // LVGL lock, waiting on `mutex`; this task holding `mutex`, waiting on the LVGL lock) -
    // exactly what happens when BT events fire rapidly (e.g. during scanning) while the app
    // is closing.
    lvgl_lock();
    ctx->lock();
    ctx->view.update();
    ctx->unlock();
    lvgl_unlock();
}

void onBtEvent(Context* ctx, const BtEvent& event) {
    auto radio_state = bluetooth::getRadioState();
    LOG_I(TAG, "Update with state %s", bluetooth::radioStateToString(radio_state));
    ctx->state.setRadioState(radio_state);
    switch (event.type) {
        case BT_EVENT_SCAN_STARTED:
            ctx->state.setScanning(true);
            break;
        case BT_EVENT_SCAN_FINISHED:
            ctx->state.setScanning(false);
            ctx->state.updateScanResults();
            ctx->state.updatePairedPeers();
            break;
        case BT_EVENT_PEER_FOUND:
            ctx->state.updateScanResults();
            break;
        case BT_EVENT_PAIR_RESULT:
            ctx->state.updatePairedPeers();
            break;
        case BT_EVENT_PROFILE_STATE_CHANGED:
            ctx->state.updateScanResults();
            ctx->state.updatePairedPeers();
            break;
        case BT_EVENT_RADIO_STATE_CHANGED:
            if (event.radio_state == BT_RADIO_STATE_ON) {
                ctx->state.updatePairedPeers();
                Device* dev = nullptr;
                if (device_get_first_active_by_type(&BLUETOOTH_TYPE, &dev) == ERROR_NONE && !bluetooth_is_scanning(dev)) {
                    bluetooth_scan_start(dev);
                }
                if (dev) {
                    device_put(dev);
                }
            }
            break;
        default:
            break;
    }

    requestViewUpdate(ctx);
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->lock();
    ctx->view.init(ctx, parent);
    ctx->view.update();
    ctx->unlock();
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx;
    ctx.appInstanceId = appInstanceId;
    ctx.bindings = (Bindings) {
        .onBtToggled = onBtToggled,
        .onScanToggled = onScanToggled,
        .onConnectPeer = onConnectPeer,
        .onDisconnectPeer = onDisconnectPeer,
        .onPairPeer = onPairPeer,
        .onForgetPeer = onForgetPeer,
    };

    // Initialise state before subscribing to avoid incoming events racing with it.
    ctx.state.setRadioState(bluetooth::getRadioState());
    Device* dev = nullptr;
    device_get_first_by_type(&BLUETOOTH_TYPE, &dev);

    ctx.state.setScanning(dev ? bluetooth_is_scanning(dev) : false);
    ctx.state.updateScanResults();
    ctx.state.updatePairedPeers();

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    // dev is started for the process lifetime once ble0 is enabled in the devicetree -
    // subscribe once here rather than resubscribing on every bluetooth::start()/stop() toggle.
    if (dev != nullptr) {
        if (bluetooth_event_subscribe(dev, &ctx.btEventSub, &event_group) == ERROR_NONE) {
            ctx.btDevice = dev;
        } else {
            LOG_W(TAG, "Failed to subscribe to BT events");
        }
    }

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    auto radio_state = bluetooth::getRadioState();
    bool can_scan = radio_state == bluetooth::RadioState::On;
    LOG_I(TAG, "Radio: %s, Scanning: %d, Can scan: %d",
        bluetooth::radioStateToString(radio_state),
        (int)(dev ? bluetooth_is_scanning(dev) : false),
        (int)can_scan);
    if (can_scan && dev && !bluetooth_is_scanning(dev)) {
        bluetooth_scan_start(dev);
    }

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

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

        if (ctx.btDevice != nullptr) {
            BtEvent bt_event {};
            while (bluetooth_event_poll(&ctx.btEventSub, &bt_event) == ERROR_NONE) {
                onBtEvent(&ctx, bt_event);
            }
        }
    }

    if (ctx.btDevice) {
        bluetooth_event_unsubscribe(ctx.btDevice, &ctx.btEventSub);
        ctx.btDevice = nullptr;
    }

    if (dev != nullptr) {
        device_put(dev);
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

uint32_t start() {
    uint32_t instanceId = 0;
    app_start(manifest.id, 0, nullptr, &instanceId);
    return instanceId;
}

extern const ::AppManifest manifest = {
    .id = "tactility.btmanage",
    .name = "Bluetooth",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::btmanage
