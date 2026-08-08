#include <Tactility/app/btpeersettings/BtPeerSettings.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/bluetooth/Bluetooth.h>
#include <Tactility/bluetooth/BluetoothPairedDevice.h>
#include <Tactility/lvgl/Style.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/device.h>
#include <tactility/drivers/bluetooth.h>
#include <tactility/log.h>

namespace tt::app::btpeersettings {

constexpr auto* TAG = "BtPeerSettings";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    std::string addrHex;
    std::array<uint8_t, 6> addr = {};
    int profileId = BT_PROFILE_HID_HOST;

    lv_obj_t* connectButton = nullptr;
    lv_obj_t* disconnectButton = nullptr;
};


bool isCurrentlyConnected(const Context* ctx) {
    for (const auto& p : bluetooth::getPairedPeers()) {
        if (p.addr == ctx->addr) return p.connected;
    }
    return false;
}

void updateViews(const Context* ctx) {
    if (isCurrentlyConnected(ctx)) {
        lv_obj_remove_flag(ctx->disconnectButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->connectButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(ctx->disconnectButton, LV_STATE_DISABLED);
    } else {
        lv_obj_add_flag(ctx->disconnectButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ctx->connectButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(ctx->connectButton, LV_STATE_DISABLED);
    }
}

void onKernelBtEvent(struct Device* /*device*/, void* context, struct BtEvent /*event*/) {
    auto* ctx = static_cast<Context*>(context);
    lvgl_lock();
    updateViews(ctx);
    lvgl_unlock();
}

void onPressConnect(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (ctx->profileId == BT_PROFILE_HID_HOST) {
        bluetooth::hidHostConnect(ctx->addr);
    } else {
        bluetooth::connect(ctx->addr, ctx->profileId);
    }
    lv_obj_add_state(lv_event_get_target_obj(event), LV_STATE_DISABLED);
}

void onPressDisconnect(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (ctx->profileId == BT_PROFILE_HID_HOST) {
        bluetooth::hidHostDisconnect();
    } else {
        bluetooth::disconnect(ctx->addr, ctx->profileId);
    }
    lv_obj_add_state(lv_event_get_target_obj(event), LV_STATE_DISABLED);
}

void onPressForget(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Result isn't tracked by launch id (matches the original's behavior) - this app only
    // ever has one dialog in flight at a time.
    alertdialog::start(ctx->appInstanceId, "Confirmation", "Forget this device?", std::vector<std::string> { "Yes", "No" });
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void onToggleAutoConnect(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    bool is_on = lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED);
    bluetooth::settings::PairedDevice device;
    if (bluetooth::settings::load(ctx->addrHex, device)) {
        device.autoConnect = is_on;
        if (!bluetooth::settings::save(device)) {
            LOG_E(TAG, "Failed to save auto-connect setting");
        }
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    bluetooth::settings::PairedDevice device;
    bool deviceLoaded = bluetooth::settings::load(ctx->addrHex, device);
    std::string title = (deviceLoaded && !device.name.empty()) ? device.name : ctx->addrHex;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, title.c_str());
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
    lvgl::obj_set_style_bg_invisible(wrapper);

    ctx->connectButton = lv_button_create(wrapper);
    lv_obj_set_width(ctx->connectButton, LV_PCT(100));
    lv_obj_add_event_cb(ctx->connectButton, onPressConnect, LV_EVENT_SHORT_CLICKED, ctx);
    auto* connect_label = lv_label_create(ctx->connectButton);
    lv_obj_align(connect_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(connect_label, "Connect");

    ctx->disconnectButton = lv_button_create(wrapper);
    lv_obj_set_width(ctx->disconnectButton, LV_PCT(100));
    lv_obj_add_event_cb(ctx->disconnectButton, onPressDisconnect, LV_EVENT_SHORT_CLICKED, ctx);
    auto* disconnect_label = lv_label_create(ctx->disconnectButton);
    lv_obj_align(disconnect_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(disconnect_label, "Disconnect");

    auto* forget_button = lv_button_create(wrapper);
    lv_obj_set_width(forget_button, LV_PCT(100));
    lv_obj_add_event_cb(forget_button, onPressForget, LV_EVENT_SHORT_CLICKED, ctx);
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
    lv_obj_add_event_cb(auto_connect_switch, onToggleAutoConnect, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_align(auto_connect_switch, LV_ALIGN_RIGHT_MID, 0, 0);

    if (deviceLoaded && device.autoConnect) {
        lv_obj_add_state(auto_connect_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(auto_connect_switch, LV_STATE_CHECKED);
    }

    updateViews(ctx);
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.addrHex = (argc > 0) ? argv[0] : std::string();

    // Load addr and profileId from stored settings - avoids manual hex parsing (std::stoul
    // throws on invalid input and exceptions are disabled).
    bluetooth::settings::PairedDevice device;
    if (bluetooth::settings::load(ctx.addrHex, device)) {
        ctx.addr = device.addr;
        ctx.profileId = device.profileId;
    }


    Device* btDevice = nullptr;
    if (device_get_first_active_by_type(&BLUETOOTH_TYPE, &btDevice) == ERROR_NONE) {
        bluetooth_add_event_callback(btDevice, &ctx, onKernelBtEvent);
        device_put(btDevice);
    }

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

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
            case APP_EVENT_RESULT:
                if (event.result.result == 0) { // 0 = Yes
                    if (isCurrentlyConnected(&ctx)) {
                        if (ctx.profileId == BT_PROFILE_HID_HOST) {
                            bluetooth::hidHostDisconnect();
                        } else {
                            bluetooth::disconnect(ctx.addr, ctx.profileId);
                        }
                    }
                    bluetooth::unpair(ctx.addr);
                    app_manager_finish(appInstanceId);
                    shouldClose = true;
                }
                app_manager_stop(event.result.launch_id);
                break;
            default:
                break;
        }
    }

    if (device_get_first_active_by_type(&BLUETOOTH_TYPE, &btDevice) == ERROR_NONE) {
        bluetooth_remove_event_callback(btDevice, onKernelBtEvent);
        device_put(btDevice);
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

void start(const std::string& addrHex) {
    const char* argv[] = { addrHex.c_str() };
    uint32_t instanceId = 0;
    app_manager_start_with_parameters(manifest.id, 1, argv, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "BtPeerSettings",
    .name = "BT Device Settings",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
};

} // namespace tt::app::btpeersettings
