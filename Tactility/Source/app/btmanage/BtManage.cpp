#include <lvgl/lvgl.h>

#include <Tactility/app/btmanage/BtManagePrivate.h>
#include <Tactility/app/btmanage/View.h>

#include <Tactility/Tactility.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/log.h>

namespace tt::app::btmanage {

constexpr auto* TAG = "BtManage";

extern const ::AppManifest manifest;


static void onBtToggled(void* context, bool requestOn) {
#if defined(CONFIG_BT_NIMBLE_ENABLED)
    auto* ctx = static_cast<Context*>(context);
    Device* dev;
    if (device_get_first_by_type(&BLUETOOTH_TYPE, &dev) == ERROR_NONE) {
        bool radio_on = bluetooth::isRadioOnOrPending(dev);
        if (requestOn && !radio_on) {
            LOG_I(TAG, "Turning on");
            if (bluetooth::start(dev)) {
                // The driver only allocates its callback list once the device is started,
                // so the registration attempted at startup (while radio was off) was a
                // no-op. Register again now that the device is actually up.
                registerDeviceCallback(ctx, dev);
            }
        } else if (!requestOn && radio_on) {
            LOG_I(TAG, "Turning off");
            if (bluetooth::stop(dev)) {
                // A completed stop frees the driver's callback list.
                forgetCallbackRegistration(ctx);
            }
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

static void onKernelBtEvent(Device* /*device*/, void* context, BtEvent event);

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

static void onKernelBtEvent(Device* /*device*/, void* context, BtEvent event) {
    // BT event callbacks can fire from the NimBLE host task (e.g. DISCONNECT during
    // nimble_port_stop shutdown). Calling onBtEvent() synchronously from the NimBLE
    // task would block it on the LVGL mutex (held by the LVGL task waiting in
    // nimble_port_stop), creating a permanent deadlock. Dispatch to the main task so
    // the NimBLE host task is never blocked by BtManage's state updates or LVGL lock.
    auto* ctx = static_cast<Context*>(context);
    // Captured while `ctx` is still guaranteed valid (the callback is only invoked while
    // registered, i.e. before appMain()'s cleanup removes it). Comparing this later -
    // without dereferencing `ctx` - lets the dispatched lambda detect a stale event from an
    // instance that has since closed (and had its Context destroyed) without a UAF: the
    // generation bump in appMain()'s cleanup always happens before window_manager_remove()
    // destroys ctx's widgets, and this dispatched lambda always re-reads the live generation
    // at run time (not at dispatch time), so a bump landing anywhere before this lambda
    // actually runs is enough to make it skip touching ctx.
    auto generation = ctx->generation;
    int expectedGeneration = generation->load();
    getMainDispatcher().dispatch([ctx, generation, expectedGeneration, event] {
        if (generation->load() != expectedGeneration) {
            return;
        }
        onBtEvent(ctx, event);
    });
}

void registerDeviceCallback(Context* ctx, Device* dev) {
    ctx->lock();
    if (ctx->btDevice == dev && !ctx->callbackRegistered) {
        // Only latch the flag on success: while the radio is off the driver has no
        // callback list yet, so this add is a silent no-op and must be retried once
        // bluetooth::start() actually brings the device up.
        if (bluetooth_add_event_callback(dev, ctx, onKernelBtEvent) == ERROR_NONE) {
            ctx->callbackRegistered = true;
        }
    }
    ctx->unlock();
}

void forgetCallbackRegistration(Context* ctx) {
    ctx->lock();
    ctx->callbackRegistered = false;
    ctx->unlock();
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->lock();
    ctx->view.init(ctx, parent);
    ctx->view.update();
    ctx->unlock();
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
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

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    ctx.btDevice = dev;
    if (ctx.btDevice) {
        registerDeviceCallback(&ctx, ctx.btDevice);
    }

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

    // Invalidate any BT event dispatched-but-not-yet-run for this instance before doing
    // anything else, so it can't race the teardown below (see onKernelBtEvent()).
    ctx.generation->fetch_add(1);

    if (ctx.btDevice) {
        if (ctx.callbackRegistered) {
            bluetooth_remove_event_callback(ctx.btDevice, onKernelBtEvent);
            ctx.callbackRegistered = false;
        }
        device_put(ctx.btDevice);
        ctx.btDevice = nullptr;
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

uint32_t start() {
    uint32_t instanceId = 0;
    app_manager_start(manifest.id, &instanceId);
    return instanceId;
}

extern const ::AppManifest manifest = {
    .id = "BtManage",
    .name = "Bluetooth",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace tt::app::btmanage
