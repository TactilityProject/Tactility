#include <Tactility/app/btpeersettings/BtPeerSettings.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/toolbar.h>

#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/lvgl/Style.h>

#include <bluetooth/bluetooth_paired_device.h>
#include <bluetooth/bluetooth_service.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/bluetooth.h>
#include <tactility/log.h>

#include <cstring>

namespace tt::app::btpeersettings {

constexpr auto* TAG = "BtPeerSettings";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    std::string addrHex;
    BtAddr addr = {};
    BtProfileId profile = BT_PROFILE_HID_HOST;

    lv_obj_t* connectButton = nullptr;
    lv_obj_t* disconnectButton = nullptr;
};


bool isCurrentlyConnected(const Context* ctx) {
    struct Query {
        const uint8_t* addr;
        bool connected;
    } query = { ctx->addr, false };
    bluetooth_service_for_each_paired_peer(&query, [](const BluetoothPeer* peer, void* context) {
        auto* query = static_cast<Query*>(context);
        if (memcmp(peer->addr, query->addr, BT_ADDR_LEN) != 0) {
            return true;
        }
        query->connected = peer->connected;
        return false;
    });
    return query.connected;
}

void updateViews(const Context* ctx) {
    if (isCurrentlyConnected(ctx)) {
        lv_obj_set_hidden(ctx->disconnectButton, false);
        lv_obj_set_hidden(ctx->connectButton, true);
        lv_obj_remove_state(ctx->disconnectButton, LV_STATE_DISABLED);
    } else {
        lv_obj_set_hidden(ctx->disconnectButton, true);
        lv_obj_set_hidden(ctx->connectButton, false);
        lv_obj_remove_state(ctx->connectButton, LV_STATE_DISABLED);
    }
}

void onBtEvent(Context* ctx) {
    lvgl_lock();
    updateViews(ctx);
    lvgl_unlock();
}

void onPressConnect(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    bluetooth_service_connect(ctx->addr, ctx->profile);
    lv_obj_add_state(lv_event_get_target_obj(event), LV_STATE_DISABLED);
}

void onPressDisconnect(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    bluetooth_service_disconnect(ctx->addr, ctx->profile);
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
    app_event_emit_close(ctx->appInstanceId);
}

void onToggleAutoConnect(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    bool is_on = lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED);
    BluetoothPairedDevice device;
    if (bluetooth_paired_device_load(ctx->addr, &device) == ERROR_NONE) {
        device.auto_connect = is_on;
        if (bluetooth_paired_device_save(&device) != ERROR_NONE) {
            LOG_E(TAG, "Failed to save auto-connect setting");
        }
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    BluetoothPairedDevice device;
    bool deviceLoaded = bluetooth_paired_device_load(ctx->addr, &device) == ERROR_NONE;
    std::string title = (deviceLoaded && device.name[0] != '\0') ? device.name : ctx->addrHex;

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

    auto* card = lvgl_card_create(wrapper);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    ctx->connectButton = lv_button_create(card);
    lv_obj_set_width(ctx->connectButton, LV_PCT(100));
    lv_obj_add_event_cb(ctx->connectButton, onPressConnect, LV_EVENT_SHORT_CLICKED, ctx);
    auto* connect_label = lv_label_create(ctx->connectButton);
    lv_obj_align(connect_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(connect_label, "Connect");

    ctx->disconnectButton = lv_button_create(card);
    lv_obj_set_width(ctx->disconnectButton, LV_PCT(100));
    lv_obj_add_event_cb(ctx->disconnectButton, onPressDisconnect, LV_EVENT_SHORT_CLICKED, ctx);
    auto* disconnect_label = lv_label_create(ctx->disconnectButton);
    lv_obj_align(disconnect_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(disconnect_label, "Disconnect");

    auto* forget_button = lv_button_create(card);
    lv_obj_set_width(forget_button, LV_PCT(100));
    lv_obj_add_event_cb(forget_button, onPressForget, LV_EVENT_SHORT_CLICKED, ctx);
    auto* forget_label = lv_label_create(forget_button);
    lv_obj_align(forget_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(forget_label, "Forget");

    // Auto-connect toggle row
    auto* auto_connect_wrapper = lv_obj_create(card);
    lv_obj_set_size(auto_connect_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(auto_connect_wrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(auto_connect_wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(auto_connect_wrapper, false);
    lvgl::obj_set_style_bg_invisible(auto_connect_wrapper);
    lv_obj_set_style_pad_all(auto_connect_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(auto_connect_wrapper, 0, LV_STATE_DEFAULT);

    auto* auto_connect_label = lv_label_create(auto_connect_wrapper);
    lv_label_set_text(auto_connect_label, "Auto-connect");
    lv_obj_set_flex_grow(auto_connect_label, 1);

    auto* auto_connect_switch = lv_switch_create(auto_connect_wrapper);
    lv_obj_add_event_cb(auto_connect_switch, onToggleAutoConnect, LV_EVENT_VALUE_CHANGED, ctx);

    if (deviceLoaded && device.auto_connect) {
        lv_obj_add_state(auto_connect_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(auto_connect_switch, LV_STATE_CHECKED);
    }

    updateViews(ctx);
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.addrHex = (argc > 0) ? argv[0] : std::string();

    BluetoothPairedDevice device;
    if (bluetooth_hex_to_addr(ctx.addrHex.c_str(), ctx.addr) && bluetooth_paired_device_load(ctx.addr, &device) == ERROR_NONE) {
        ctx.profile = static_cast<BtProfileId>(device.profile);
    }


    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    Device* btDevice = nullptr;
    BtEventSubscription btSub {};
    if (device_get_first_active_by_type(&BLUETOOTH_TYPE, &btDevice) == ERROR_NONE) {
        if (bluetooth_event_subscribe(btDevice, &btSub, &event_group) != ERROR_NONE) {
            device_put(btDevice);
            btDevice = nullptr;
        }
    }

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    shouldClose = true;
                    break;
                case APP_EVENT_RESULT:
                    if (event.result.result == 0) { // 0 = Yes
                        if (isCurrentlyConnected(&ctx)) {
                            bluetooth_service_disconnect(ctx.addr, ctx.profile);
                        }
                        bluetooth_service_unpair(ctx.addr);
                        shouldClose = true;
                    }
                    app_manager_stop(event.result.launch_id);
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }

        if (btDevice != nullptr) {
            BtEvent bt_event {};
            while (bluetooth_event_poll(&btSub, &bt_event) == ERROR_NONE) {
                onBtEvent(&ctx);
            }
        }
    }

    if (btDevice != nullptr) {
        bluetooth_event_unsubscribe(btDevice, &btSub);
        device_put(btDevice);
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

void start(const std::string& addrHex) {
    const char* argv[] = { addrHex.c_str() };
    uint32_t instanceId = 0;
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_context_set_arguments_ext(&context, 1, argv);
    app_start_with_context(&context, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "tactility.btpeersettings",
    .name = "BT Device Settings",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace tt::app::btpeersettings
