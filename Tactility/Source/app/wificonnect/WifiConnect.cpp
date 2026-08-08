#include <Tactility/app/wificonnect/WifiConnect.h>

#include <Tactility/service/wifi/Wifi.h>
#include <Tactility/service/wifi/WifiApSettings.h>
#include <Tactility/service/wifi/WifiGlobals.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/spinner.h>
#include <lvgl/widgets/toolbar.h>

#include <tactility/log.h>

#include <lvgl.h>
#include <cstring>

namespace tt::app::wificonnect {

constexpr auto* TAG = "WifiConnect";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;

    std::string initialSsid;
    std::string initialPassword;

    // Touched only from the LVGL task: directly by onConnectPressed() (an LVGL event callback,
    // which already runs with the LVGL lock held), and by onWifiEvent() (a wifi-pubsub
    // callback running on some other thread) which explicitly wraps its touches in
    // lvgl_lock()/lvgl_unlock() - see WifiApSettings.cpp for the same convention.
    bool connecting = false;
    bool connectionError = false;

    lv_obj_t* ssid_textarea = nullptr;
    lv_obj_t* ssid_error = nullptr;
    lv_obj_t* password_textarea = nullptr;
    lv_obj_t* password_error = nullptr;
    lv_obj_t* connect_button = nullptr;
    lv_obj_t* remember_switch = nullptr;
    lv_obj_t* connecting_spinner = nullptr;
    lv_obj_t* connection_error = nullptr;

    PubSub<service::wifi::WifiEvent>::SubscriptionHandle wifiSubscription = nullptr;
};


void updateView(Context* ctx);
void resetErrors(Context* ctx);
void setLoading(Context* ctx, bool loading);

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

// Runs on the wifi service's pubsub thread, not the LVGL task or this app's own thread.
void onWifiEvent(Context* ctx, service::wifi::WifiEvent event) {
    bool shouldClose = false;

    lvgl_lock();
    if (event.type == WIFI_EVENT_TYPE_STATION_CONNECTION_RESULT) {
        if (event.connection_error == WIFI_STATION_CONNECTION_ERROR_NONE) {
            if (ctx->connecting) {
                ctx->connecting = false;
                shouldClose = true;
            }
        } else {
            if (ctx->connecting) {
                ctx->connecting = false;
                ctx->connectionError = true;
                updateView(ctx);
            }
        }
    }
    updateView(ctx);
    lvgl_unlock();

    if (shouldClose) {
        // Async, non-blocking - same reasoning as onBackPressed() (must not call
        // app_manager_stop() on ourselves); safe to call from any thread.
        AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
        app_event_emit(ctx->appInstanceId, &closeEvent);
    }
}

void resetErrors(Context* ctx) {
    lv_obj_add_flag(ctx->password_error, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ctx->ssid_error, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ctx->connection_error, LV_OBJ_FLAG_HIDDEN);
}

void setLoading(Context* ctx, bool loading) {
    if (loading) {
        lv_obj_add_flag(ctx->connect_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ctx->connecting_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(ctx->password_textarea, LV_STATE_DISABLED);
        lv_obj_add_state(ctx->ssid_textarea, LV_STATE_DISABLED);
        lv_obj_add_state(ctx->remember_switch, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_flag(ctx->connect_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->connecting_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(ctx->password_textarea, LV_STATE_DISABLED);
        lv_obj_remove_state(ctx->ssid_textarea, LV_STATE_DISABLED);
        lv_obj_remove_state(ctx->remember_switch, LV_STATE_DISABLED);
    }
}

void updateView(Context* ctx) {
    if (ctx->connectionError) {
        setLoading(ctx, false);
        resetErrors(ctx);
        lv_label_set_text(ctx->connection_error, "Connection failed");
        lv_obj_remove_flag(ctx->connection_error, LV_OBJ_FLAG_HIDDEN);
    }
}

void onConnectPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));

    ctx->connectionError = false;
    resetErrors(ctx);

    const char* ssid = lv_textarea_get_text(ctx->ssid_textarea);
    size_t ssid_len = strlen(ssid);
    if (ssid_len > TT_WIFI_SSID_LIMIT) {
        LOG_E(TAG, "SSID too long");
        lv_label_set_text(ctx->ssid_error, "SSID too long");
        lv_obj_remove_flag(ctx->ssid_error, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const char* password = lv_textarea_get_text(ctx->password_textarea);
    size_t password_len = strlen(password);
    if (password_len > TT_WIFI_CREDENTIALS_PASSWORD_LIMIT) {
        LOG_E(TAG, "Password too long");
        lv_label_set_text(ctx->password_error, "Password too long");
        lv_obj_remove_flag(ctx->password_error, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    bool store = lv_obj_get_state(ctx->remember_switch) & LV_STATE_CHECKED;

    setLoading(ctx, true);

    service::wifi::settings::WifiApSettings settings;
    settings.password = password;
    settings.ssid = ssid;
    settings.channel = 0;
    settings.autoConnect = TT_WIFI_AUTO_CONNECT; // No UI yet, so use global setting

    ctx->connecting = true;
    service::wifi::connect(settings, store);
}

void createBottomButtons(Context* ctx, lv_obj_t* parent) {
    auto* button_container = lv_obj_create(parent);
    lv_obj_set_width(button_container, LV_PCT(100));
    lv_obj_set_height(button_container, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(button_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_gap(button_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button_container, 0, LV_STATE_DEFAULT);

    ctx->remember_switch = lv_switch_create(button_container);
    lv_obj_add_state(ctx->remember_switch, LV_STATE_CHECKED);
    lv_obj_align(ctx->remember_switch, LV_ALIGN_LEFT_MID, 0, 0);

    auto* remember_label = lv_label_create(button_container);
    lv_label_set_text(remember_label, "Remember");
    lv_obj_align(remember_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_align_to(remember_label, ctx->remember_switch, LV_ALIGN_OUT_RIGHT_MID, 4, 0);

    ctx->connecting_spinner = lvgl_spinner_create(button_container);
    lv_obj_align(ctx->connecting_spinner, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(ctx->connecting_spinner, LV_OBJ_FLAG_HIDDEN);

    ctx->connect_button = lv_btn_create(button_container);
    auto* connect_label = lv_label_create(ctx->connect_button);
    lv_label_set_text(connect_label, "Connect");
    lv_obj_align(ctx->connect_button, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(ctx->connect_button, onConnectPressed, LV_EVENT_SHORT_CLICKED, ctx);
}

// TODO: Standardize dialogs
void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    ctx->wifiSubscription = service::wifi::getPubsub()->subscribe([ctx](auto event) {
        onWifiEvent(ctx, event);
    });

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Wi-Fi Connect");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);

    // SSID

    auto* ssid_wrapper = lv_obj_create(wrapper);
    lv_obj_set_width(ssid_wrapper, LV_PCT(100));
    lv_obj_set_height(ssid_wrapper, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(ssid_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_gap(ssid_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ssid_wrapper, 0, LV_STATE_DEFAULT);

    auto* ssid_label_wrapper = lv_obj_create(ssid_wrapper);
    lv_obj_set_width(ssid_label_wrapper, LV_PCT(50));
    lv_obj_set_height(ssid_label_wrapper, LV_SIZE_CONTENT);
    lv_obj_align(ssid_label_wrapper, LV_ALIGN_LEFT_MID, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ssid_label_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ssid_label_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ssid_label_wrapper, 0, LV_STATE_DEFAULT);

    auto* ssid_label = lv_label_create(ssid_label_wrapper);
    lv_label_set_text(ssid_label, "Network:");

    ctx->ssid_textarea = lv_textarea_create(ssid_wrapper);
    lv_textarea_set_one_line(ctx->ssid_textarea, true);
    lv_obj_align(ctx->ssid_textarea, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_width(ctx->ssid_textarea, LV_PCT(50));

    ctx->ssid_error = lv_label_create(wrapper);
    lv_obj_set_style_text_color(ctx->ssid_error, lv_color_make(255, 50, 50), LV_STATE_DEFAULT);
    lv_obj_add_flag(ctx->ssid_error, LV_OBJ_FLAG_HIDDEN);

    // Password

    auto* password_wrapper = lv_obj_create(wrapper);
    lv_obj_set_width(password_wrapper, LV_PCT(100));
    lv_obj_set_height(password_wrapper, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(password_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_gap(password_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(password_wrapper, 0, LV_STATE_DEFAULT);

    auto* password_label_wrapper = lv_obj_create(password_wrapper);
    lv_obj_set_width(password_label_wrapper, LV_PCT(50));
    lv_obj_set_height(password_label_wrapper, LV_SIZE_CONTENT);
    lv_obj_align_to(password_label_wrapper, password_wrapper, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_border_width(password_label_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(password_label_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(password_label_wrapper, 0, LV_STATE_DEFAULT);

    auto* password_label = lv_label_create(password_label_wrapper);
    lv_label_set_text(password_label, "Password:");

    ctx->password_textarea = lv_textarea_create(password_wrapper);
    lv_textarea_set_one_line(ctx->password_textarea, true);
    lv_textarea_set_password_mode(ctx->password_textarea, true);
    lv_obj_align(ctx->password_textarea, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_width(ctx->password_textarea, LV_PCT(50));

    ctx->password_error = lv_label_create(wrapper);
    lv_obj_set_style_text_color(ctx->password_error, lv_color_make(255, 50, 50), LV_STATE_DEFAULT);
    lv_obj_add_flag(ctx->password_error, LV_OBJ_FLAG_HIDDEN);

    // Connection error
    ctx->connection_error = lv_label_create(wrapper);
    lv_obj_set_style_text_color(ctx->connection_error, lv_color_make(255, 50, 50), LV_STATE_DEFAULT);
    lv_obj_add_flag(ctx->connection_error, LV_OBJ_FLAG_HIDDEN);

    // Bottom buttons
    createBottomButtons(ctx, wrapper);

    // Init from app parameters
    if (!ctx->initialSsid.empty()) {
        lv_textarea_set_text(ctx->ssid_textarea, ctx->initialSsid.c_str());
        lv_group_focus_obj(ctx->password_textarea);
    }
    if (!ctx->initialPassword.empty()) {
        lv_textarea_set_text(ctx->password_textarea, ctx->initialPassword.c_str());
    }
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.initialSsid = (argc > 0) ? argv[0] : std::string();
    ctx.initialPassword = (argc > 1) ? argv[1] : std::string();

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

void start(const std::string& ssid, const std::string& password) {
    const char* argv[] = { ssid.c_str(), password.c_str() };
    uint32_t instanceId = 0;
    app_manager_start_with_parameters(manifest.id, 2, argv, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "WifiConnect",
    .name = "Wi-Fi Connect",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
};

} // namespace
