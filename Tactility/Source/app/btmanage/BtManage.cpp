#include <lvgl/lvgl.h>

#include <Tactility/app/btmanage/BtManagePrivate.h>
#include <Tactility/app/btmanage/View.h>

#include <bluetooth/bluetooth_service.h>
#include <bluetooth/bluetooth_settings.h>

#include <app/event.h>
#include <app/location.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/device.h>
#include <tactility/file_ops.h>
#include <tactility/log.h>

#include <new>

namespace tt::app::btmanage {

constexpr auto* TAG = "BtManage";

extern const ::AppManifest manifest;

static BtRadioState getRadioState(Device* dev) {
    BtRadioState state = BT_RADIO_STATE_OFF;
    if (dev != nullptr) {
        bluetooth_get_radio_state(dev, &state);
    }
    return state;
}

struct SaveEnabledJob {
    bool enabled;
};

/** File operation: it runs on the file operations task, which frees the job */
static void saveEnabledInBackground(void* context) {
    auto* job = static_cast<SaveEnabledJob*>(context);
    bluetooth_settings_set_enable(job->enabled);
    delete job;
}

/** Runs on the app task: enabling the radio writes to flash, which the LVGL task can't do with its stack in external RAM */
static void setRadioEnabled(bool requestOn) {
    // Queued, so file IO doesn't block the app task
    auto* job = new (::std::nothrow) SaveEnabledJob { requestOn };
    if (job == nullptr || file_ops_queue(saveEnabledInBackground, job, nullptr) != ERROR_NONE) {
        LOG_E(TAG, "Failed to queue saving the Bluetooth state");
        delete job;
    }
    Device* dev;
    if (device_get_first_by_type(&BLUETOOTH_TYPE, &dev) == ERROR_NONE) {
        bool radio_on = bluetooth_is_radio_on_or_pending(dev);
        if (requestOn && !radio_on) {
            LOG_I(TAG, "Turning on");
            bluetooth_service_set_radio_enabled(true);
        } else if (!requestOn && radio_on) {
            LOG_I(TAG, "Turning off");
            bluetooth_service_set_radio_enabled(false);
        }
        device_put(dev);
    } else {
        LOG_W(TAG, "Toggle: No bluetooth device found");
    }
}

static void onBtToggled(void* context, bool requestOn) {
    auto* ctx = static_cast<Context*>(context);
    ctx->radioRequest = requestOn ? 1 : 0;
    task_event_group_signal(ctx->eventGroup, ctx->requestBit);
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

static void onConnectPeer(const BtAddr addr, BtProfileId profile) {
    bluetooth_service_connect(addr, profile);
}

static void onDisconnectPeer(const BtAddr addr, BtProfileId profile) {
    bluetooth_service_disconnect(addr, profile);
}

static void onPairPeer(void* /*context*/, const BtAddr addr) {
    // Clicking an unrecognised scan result initiates a HID host connection.
    // Bond exchange happens automatically during the first connection.
    bluetooth_service_connect(addr, BT_PROFILE_HID_HOST);
}

static void onForgetPeer(const BtAddr addr) {
    bluetooth_service_unpair(addr);
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
    auto radio_state = getRadioState(ctx->btDevice);
    LOG_I(TAG, "Update with state %d", static_cast<int>(radio_state));
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

// Runs with the LVGL lock held and must not take other locks
void destroyWidgets(void* userData) {
    static_cast<Context*>(userData)->view.destroyWidgets();
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
    Device* dev = nullptr;
    device_get_first_by_type(&BLUETOOTH_TYPE, &dev);
    ctx.state.setRadioState(getRadioState(dev));

    ctx.state.setScanning(dev ? bluetooth_is_scanning(dev) : false);
    ctx.state.updateScanResults();
    ctx.state.updatePairedPeers();

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx.requestBit) == ERROR_NONE);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    // dev is started for the process lifetime once ble0 is enabled in the devicetree -
    // subscribe once here rather than resubscribing on every radio toggle.
    if (dev != nullptr) {
        if (bluetooth_event_subscribe(dev, &ctx.btEventSub, &event_group) == ERROR_NONE) {
            ctx.btDevice = dev;
        } else {
            LOG_W(TAG, "Failed to subscribe to BT events");
        }
    }

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

    auto radio_state = getRadioState(dev);
    bool can_scan = radio_state == BT_RADIO_STATE_ON;
    LOG_I(TAG, "Radio: %d, Scanning: %d, Can scan: %d",
        static_cast<int>(radio_state),
        (int)(dev ? bluetooth_is_scanning(dev) : false),
        (int)can_scan);
    if (can_scan && dev && !bluetooth_is_scanning(dev)) {
        bluetooth_scan_start(dev);
    }

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        const int radio_request = ctx.radioRequest.exchange(-1);
        if (radio_request != -1) {
            setRadioEnabled(radio_request == 1);
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
    task_event_group_release_bit(&event_group, ctx.requestBit);
    task_event_group_destruct(&event_group);

    return 0;
}

uint32_t start() {
    uint32_t instanceId = 0;
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_with_context(&context, &instanceId);
    return instanceId;
}

extern const ::AppManifest manifest = {
    .id = "tactility.btmanage",
    .name = "Bluetooth",
    .category = APP_CATEGORY_SETTINGS,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {},
};

} // namespace tt::app::btmanage
