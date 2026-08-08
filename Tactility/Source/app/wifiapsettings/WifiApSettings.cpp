#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/service/wifi/Wifi.h>
#include <Tactility/service/wifi/WifiApSettings.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

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

    uint32_t forgetDialogId = 0;
    PubSub<service::wifi::WifiEvent>::SubscriptionHandle wifiSubscription = nullptr;
};


void updateViews(Context* ctx);

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
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

void updateViews(Context* ctx) {
    updateConnectButton(ctx);
    updateBusySpinner(ctx);
}

void requestViewUpdate(Context* ctx) {
    lvgl_lock();
    updateViews(ctx);
    lvgl_unlock();
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    ctx->wifiSubscription = service::wifi::getPubsub()->subscribe([ctx](auto) {
        requestViewUpdate(ctx);
    });

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

    auto* forget_button = lv_button_create(wrapper);
    lv_obj_set_width(forget_button, LV_PCT(100));
    lv_obj_add_event_cb(forget_button, onPressForget, LV_EVENT_SHORT_CLICKED, ctx);
    auto* forget_button_label = lv_label_create(forget_button);
    lv_obj_align(forget_button_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(forget_button_label, "Forget");

    // Auto-connect

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

    service::wifi::settings::WifiApSettings settings;
    if (service::wifi::settings::load(ctx->ssid.c_str(), settings)) {
        if (settings.autoConnect) {
            lv_obj_add_state(auto_connect_switch, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(auto_connect_switch, LV_STATE_CHECKED);
        }
    } else {
        LOG_W(TAG, "No settings found");
        lv_obj_add_flag(forget_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(auto_connect_wrapper, LV_OBJ_FLAG_HIDDEN);
    }

    updateViews(ctx);
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.ssid = (argc > 0) ? argv[0] : std::string();

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
                        app_manager_finish(appInstanceId);
                        shouldClose = true;
                    }
                }
                app_manager_stop(event.result.launch_id);
                break;
            default:
                break;
        }
    }

    if (ctx.wifiSubscription != nullptr) {
        service::wifi::getPubsub()->unsubscribe(ctx.wifiSubscription);
    }
    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

void start(const std::string& ssid) {
    const char* argv[] = { ssid.c_str() };
    uint32_t instanceId = 0;
    app_manager_start_with_parameters(manifest.id, 1, argv, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "WifiApSettings",
    .name = "Wi-Fi AP Settings",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
};

} // namespace
