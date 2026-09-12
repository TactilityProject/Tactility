#include <Tactility/app/wificonnect/WifiConnect.h>

#include <Tactility/service/wifi/Wifi.h>
#include <Tactility/service/wifi/WifiApSettings.h>
#include <Tactility/service/wifi/WifiGlobals.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/spinner.h>
#include <lvgl/widgets/toolbar.h>

#include <tactility/check.h>
#include <tactility/device.h>
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

    // Set once in appMain() before subscribing, left null if this device has no WiFi driver
    Device* wifiDevice = nullptr;
    WifiEventSubscription wifiEventSub {};
};


void updateView(Context* ctx);
void resetErrors(Context* ctx);
void setLoading(Context* ctx, bool loading);

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void onWifiEvent(Context* ctx, WifiEvent event) {
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
        app_event_emit_close(ctx->appInstanceId);
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
    if (ctx->connect_button == nullptr) {
        // Buried (e.g. this window's own connecting state closed it, or a future dialog opens on top)
        return;
    }
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

// Runs with the LVGL lock already held, possibly on another app's thread - see
// WindowDestroyWidgetsFn's warnings. Must stay lock-free: only nulls pointers.
void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->ssid_textarea = nullptr;
    ctx->ssid_error = nullptr;
    ctx->password_textarea = nullptr;
    ctx->password_error = nullptr;
    ctx->connect_button = nullptr;
    ctx->remember_switch = nullptr;
    ctx->connecting_spinner = nullptr;
    ctx->connection_error = nullptr;
}

// TODO: Standardize dialogs
void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

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

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.initialSsid = (argc > 0) ? argv[0] : std::string();
    ctx.initialPassword = (argc > 1) ? argv[1] : std::string();

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

void start(const std::string& ssid, const std::string& password) {
    const char* argv[] = { ssid.c_str(), password.c_str() };
    uint32_t instanceId = 0;
    app_start(manifest.id, 2, argv, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "tactility.wificonnect",
    .name = "Wi-Fi Connect",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace
