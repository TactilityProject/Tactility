#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/service/wifi/Wifi.h>
#include <Tactility/service/wifi/WifiApSettings.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/log.h>

namespace tt::app::wifiapsettings {

constexpr auto* TAG = "WifiApSettings";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    std::string ssid;

    lv_obj_t* busySpinner = nullptr;
    lv_obj_t* connectButton = nullptr;
    lv_obj_t* disconnectButton = nullptr;
    lv_obj_t* autoConnectSwitch = nullptr;
    lv_obj_t* forgetButton = nullptr;
    lv_obj_t* autoConnectWrapper = nullptr;

    uint32_t forgetDialogId = 0;
    // Set once in appMain() before subscribing, left null if this device has no WiFi driver
    Device* wifiDevice = nullptr;
    WifiEventSubscription wifiEventSub {};
};


void updateViews(Context* ctx);

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onPressForget(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->forgetDialogId = alertdialog::start(ctx->appInstanceId, "Confirmation", "Forget the Wi-Fi access point?", std::vector<std::string> { "Yes", "No" });
}

void onToggleAutoConnect(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* enable_switch = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool is_on = lv_obj_has_state(enable_switch, LV_STATE_CHECKED);

    service::wifi::settings::WifiApSettings settings;
    if (service::wifi::settings::load(ctx->ssid.c_str(), settings)) {
        settings.autoConnect = is_on;
        if (!service::wifi::settings::save(settings)) {
            LOG_E(TAG, "Failed to save settings");
        }
    } else {
        LOG_E(TAG, "Failed to load settings");
    }
}

void onPressConnect(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    service::wifi::settings::WifiApSettings settings;
    if (service::wifi::settings::load(ctx->ssid.c_str(), settings)) {
        auto* button = lv_event_get_target_obj(event);
        lv_obj_add_state(button, LV_STATE_DISABLED);
        service::wifi::connect(settings, false);
    }
}

void onPressDisconnect(lv_event_t*) {
    if (service::wifi::getRadioState() == service::wifi::RadioState::ConnectionActive) {
        service::wifi::disconnect();
    }
}

void updateConnectButton(Context* ctx) {
    if (service::wifi::getConnectionTarget() == ctx->ssid && service::wifi::getRadioState() == service::wifi::RadioState::ConnectionActive) {
        lv_obj_remove_flag(ctx->disconnectButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->connectButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(ctx->disconnectButton, LV_STATE_DISABLED);
    } else {
        lv_obj_add_flag(ctx->disconnectButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ctx->connectButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(ctx->connectButton, LV_STATE_DISABLED);
    }
}

void updateBusySpinner(Context* ctx) {
    if (service::wifi::getRadioState() == service::wifi::RadioState::ConnectionPending) {
        lv_obj_remove_flag(ctx->busySpinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->busySpinner, LV_OBJ_FLAG_HIDDEN);
    }
}

// Touches the filesystem (service::wifi::settings::load()) - callers must not run this on the
// LVGL task (see updateViews()'s callers).
void updateAutoConnectSection(Context* ctx) {
    service::wifi::settings::WifiApSettings settings;
    if (service::wifi::settings::load(ctx->ssid.c_str(), settings)) {
        if (settings.autoConnect) {
            lv_obj_add_state(ctx->autoConnectSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(ctx->autoConnectSwitch, LV_STATE_CHECKED);
        }
        lv_obj_remove_flag(ctx->forgetButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ctx->autoConnectWrapper, LV_OBJ_FLAG_HIDDEN);
    } else {
        LOG_W(TAG, "No settings found");
        lv_obj_add_flag(ctx->forgetButton, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->autoConnectWrapper, LV_OBJ_FLAG_HIDDEN);
    }
}

// Runs on whichever thread calls it, not necessarily the LVGL task
void updateViews(Context* ctx) {
    if (ctx->connectButton == nullptr) {
        // Buried (e.g. the forget confirmation dialog opened on top)
        return;
    }
    updateConnectButton(ctx);
    updateBusySpinner(ctx);
    updateAutoConnectSection(ctx);
}

void requestViewUpdate(Context* ctx) {
    lvgl_lock();
    updateViews(ctx);
    lvgl_unlock();
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->busySpinner = nullptr;
    ctx->connectButton = nullptr;
    ctx->disconnectButton = nullptr;
    ctx->autoConnectSwitch = nullptr;
    ctx->forgetButton = nullptr;
    ctx->autoConnectWrapper = nullptr;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, ctx->ssid.c_str());
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    ctx->busySpinner = lvgl_toolbar_add_spinner_action(toolbar);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
    lvgl::obj_set_style_bg_invisible(wrapper);

    ctx->disconnectButton = lv_button_create(wrapper);
    lv_obj_set_width(ctx->disconnectButton, LV_PCT(100));
    lv_obj_add_event_cb(ctx->disconnectButton, onPressDisconnect, LV_EVENT_SHORT_CLICKED, ctx);
    auto* disconnect_label = lv_label_create(ctx->disconnectButton);
    lv_obj_align(disconnect_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(disconnect_label, "Disconnect");

    ctx->connectButton = lv_button_create(wrapper);
    lv_obj_set_width(ctx->connectButton, LV_PCT(100));
    lv_obj_add_event_cb(ctx->connectButton, onPressConnect, LV_EVENT_SHORT_CLICKED, ctx);
    auto* connect_label = lv_label_create(ctx->connectButton);
    lv_obj_align(connect_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(connect_label, "Connect");

    // Forget

    ctx->forgetButton = lv_button_create(wrapper);
    lv_obj_set_width(ctx->forgetButton, LV_PCT(100));
    lv_obj_add_event_cb(ctx->forgetButton, onPressForget, LV_EVENT_SHORT_CLICKED, ctx);
    auto* forget_button_label = lv_label_create(ctx->forgetButton);
    lv_obj_align(forget_button_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(forget_button_label, "Forget");
    lv_obj_add_flag(ctx->forgetButton, LV_OBJ_FLAG_HIDDEN); // shown by updateAutoConnectSection()

    // Auto-connect

    ctx->autoConnectWrapper = lv_obj_create(wrapper);
    lv_obj_set_size(ctx->autoConnectWrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lvgl::obj_set_style_bg_invisible(ctx->autoConnectWrapper);
    lv_obj_set_style_pad_all(ctx->autoConnectWrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ctx->autoConnectWrapper, 0, LV_STATE_DEFAULT);
    lv_obj_add_flag(ctx->autoConnectWrapper, LV_OBJ_FLAG_HIDDEN); // shown by updateAutoConnectSection()

    auto* auto_connect_label = lv_label_create(ctx->autoConnectWrapper);
    lv_label_set_text(auto_connect_label, "Auto-connect");
    lv_obj_align(auto_connect_label, LV_ALIGN_LEFT_MID, 0, 0);

    ctx->autoConnectSwitch = lv_switch_create(ctx->autoConnectWrapper);
    lv_obj_add_event_cb(ctx->autoConnectSwitch, onToggleAutoConnect, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_align(ctx->autoConnectSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.ssid = (argc > 0) ? argv[0] : std::string();

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

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

    // The file-I/O-touching part of the view (updateAutoConnectSection()'s settings::load())
    // runs here, on this app's own task, not the LVGL task createWidgets()
    requestViewUpdate(&ctx);

    bool shouldClose = false;
    while (!shouldClose) {
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
                case APP_EVENT_RESULT:
                    if (event.result.launch_id == ctx.forgetDialogId && event.result.result == 0) { // 0 = Yes
                        if (!service::wifi::settings::remove(ctx.ssid.c_str())) {
                            LOG_E(TAG, "Failed to remove SSID");
                        } else {
                            LOG_I(TAG, "Removed SSID");
                            if (
                                service::wifi::getRadioState() == service::wifi::RadioState::ConnectionActive &&
                                service::wifi::getConnectionTarget() == ctx.ssid
                            ) {
                                service::wifi::disconnect();
                            }
                            shouldClose = true;
                        }
                    }
                    app_manager_stop(event.result.launch_id);
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }

        if (ctx.wifiDevice != nullptr) {
            WifiEvent wifi_event {};
            bool wifi_event_received = false;
            while (wifi_event_poll(&ctx.wifiEventSub, &wifi_event) == ERROR_NONE) {
                wifi_event_received = true;
            }
            if (wifi_event_received) {
                requestViewUpdate(&ctx);
            }
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

void start(const std::string& ssid) {
    const char* argv[] = { ssid.c_str() };
    uint32_t instanceId = 0;
    app_start(manifest.id, 1, argv, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "tactility.wifiapsettings",
    .name = "Wi-Fi AP Settings",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace
