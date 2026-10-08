#include <Tactility/Tactility.h>
#include <Tactility/settings/WebServerSettings.h>
#include <Tactility/service/webserver/WebServerService.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/wifi.h>
#include <tactility/log.h>

#include <lvgl.h>
#include <lvgl/lvgl.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::webserversettings {

constexpr auto* TAG = "WebServerSettingsApp";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;

    settings::webserver::WebServerSettings wsSettings;
    settings::webserver::WebServerSettings originalSettings;
    bool updated = false;
    bool wifiSettingsChanged = false;
    lv_obj_t* wifiModeChips = nullptr;
    // Shown in access point mode
    lv_obj_t* apGroup = nullptr;
    // Shown when the access point isn't an open network
    lv_obj_t* apPasswordRow = nullptr;
    // Shown when authentication is required
    lv_obj_t* authCard = nullptr;
    // Shown when the web server is enabled
    lv_obj_t* urlCard = nullptr;
    lv_obj_t* textAreaApPassword = nullptr;
    lv_obj_t* switchApOpenNetwork = nullptr;
    lv_obj_t* switchWebServerEnabled = nullptr;
    lv_obj_t* switchWebServerAuthEnabled = nullptr;
    lv_obj_t* textAreaWebServerUsername = nullptr;
    lv_obj_t* textAreaWebServerPassword = nullptr;
    lv_obj_t* labelUrl = nullptr;
    lv_obj_t* labelUrlValue = nullptr;
};


void updateUrlDisplay(Context* ctx);
void createWidgets(lv_obj_t* parent, void* userData);

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

/** Shows the widgets of the enabled features. Requires the LVGL lock. */
void updateVisibility(Context* ctx, bool accessPoint, bool openNetwork, bool authEnabled, bool serverEnabled) {
    const uint32_t chip_count = lv_obj_get_child_count(ctx->wifiModeChips);
    for (uint32_t i = 0; i < chip_count; i++) {
        lv_obj_set_state(lv_obj_get_child(ctx->wifiModeChips, static_cast<int32_t>(i)), LV_STATE_CHECKED, (i == 1) == accessPoint);
    }
    lv_obj_set_hidden(ctx->apGroup, !accessPoint);
    lv_obj_set_hidden(ctx->apPasswordRow, openNetwork);
    lv_obj_set_hidden(ctx->authCard, !authEnabled);
    lv_obj_set_hidden(ctx->urlCard, !serverEnabled);
}

void updateVisibility(Context* ctx) {
    updateVisibility(ctx,
        lv_obj_has_state(lv_obj_get_child(ctx->wifiModeChips, 1), LV_STATE_CHECKED),
        lv_obj_has_state(ctx->switchApOpenNetwork, LV_STATE_CHECKED),
        lv_obj_has_state(ctx->switchWebServerAuthEnabled, LV_STATE_CHECKED),
        lv_obj_has_state(ctx->switchWebServerEnabled, LV_STATE_CHECKED));
}

void onWifiModeChipPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    // Chip 0 is station mode, chip 1 is access point mode
    const uint32_t index = lv_obj_get_index(lv_event_get_target_obj(e));
    updateVisibility(ctx, index == 1,
        lv_obj_has_state(ctx->switchApOpenNetwork, LV_STATE_CHECKED),
        lv_obj_has_state(ctx->switchWebServerAuthEnabled, LV_STATE_CHECKED),
        lv_obj_has_state(ctx->switchWebServerEnabled, LV_STATE_CHECKED));
    getMainDispatcher().dispatch([ctx, index] {
        ctx->wsSettings.wifiMode = static_cast<settings::webserver::WiFiMode>(index);
        ctx->updated = true;
        ctx->wifiSettingsChanged = true;
        lvgl_lock();
        updateUrlDisplay(ctx);
        lvgl_unlock();
    });
}

void onWebServerEnabledSwitch(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    bool enabled = lv_obj_has_state(ctx->switchWebServerEnabled, LV_STATE_CHECKED);
    updateVisibility(ctx);
    getMainDispatcher().dispatch([ctx, enabled] {
        ctx->wsSettings.webServerEnabled = enabled;
        ctx->updated = true;
        lvgl_lock();
        updateUrlDisplay(ctx);
        lvgl_unlock();

        // Apply immediately instead of waiting for app exit
        const auto copy = ctx->wsSettings;
        if (!settings::webserver::save(copy)) {
            LOG_W(TAG, "Failed to persist WebServer settings; changes may be lost on reboot");
        }
        service::webserver::getPubsub()->publish(service::webserver::WebServerEvent::WebServerSettingsChanged);
        LOG_I(TAG, "WebServer %s", enabled ? "enabling..." : "disabling...");
        service::webserver::setWebServerEnabled(enabled);
    });
}

void onWebServerAuthEnabledSwitch(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    bool enabled = lv_obj_has_state(ctx->switchWebServerAuthEnabled, LV_STATE_CHECKED);
    updateVisibility(ctx);

    getMainDispatcher().dispatch([ctx, enabled] {
        ctx->wsSettings.webServerAuthEnabled = enabled;
        ctx->updated = true;
    });
}

void onCredentialChanged(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    getMainDispatcher().dispatch([ctx] {
        ctx->updated = true;
    });
}

void onApPasswordChanged(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    getMainDispatcher().dispatch([ctx] {
        ctx->updated = true;
        ctx->wifiSettingsChanged = true;
    });
}

void onApOpenNetworkSwitch(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    bool openNetwork = lv_obj_has_state(ctx->switchApOpenNetwork, LV_STATE_CHECKED);
    updateVisibility(ctx);

    getMainDispatcher().dispatch([ctx, openNetwork] {
        ctx->wsSettings.apOpenNetwork = openNetwork;
        ctx->updated = true;
        ctx->wifiSettingsChanged = true;
    });
}

void updateUrlDisplay(Context* ctx) {
    if (!ctx->labelUrlValue) return;

    if (!ctx->wsSettings.webServerEnabled) {
        lv_label_set_text(ctx->labelUrlValue, "Disabled");
        return;
    }

    std::string url = "http://";

    if (ctx->wsSettings.wifiMode == settings::webserver::WiFiMode::AccessPoint) {
        // AP mode - always 192.168.4.1
        url += "192.168.4.1";
    } else {
        // Station mode - try to get actual IP
        char ip[16] = {};
        Device* wifi_device = nullptr;
        if (device_get_first_by_type(&WIFI_TYPE, &wifi_device) == ERROR_NONE) {
            wifi_station_get_ipv4_address(wifi_device, ip);
            device_put(wifi_device);
        }
        if (ip[0] != '\0') {
            url += ip;
        } else {
            url = "Not connected";
        }
    }

    if (url.starts_with("http://")) {
        if (ctx->wsSettings.webServerPort != 80) {
            url += ":" + std::to_string(ctx->wsSettings.webServerPort);
        }
    }

    lv_label_set_text(ctx->labelUrlValue, url.c_str());
}

lv_obj_t* createCard(lv_obj_t* parent) {
    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    return card;
}

/** A transparent column of rows, to show or hide them together */
lv_obj_t* createGroup(lv_obj_t* parent) {
    auto* group = lv_obj_create(parent);
    lv_obj_set_size(group, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(group, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(group, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(group, false);
    return group;
}

/** A transparent row: "Title          [content]" */
lv_obj_t* createRow(lv_obj_t* parent, const char* title) {
    auto* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(row, false);
    auto* label = lv_label_create(row);
    lv_label_set_text(label, title);
    lv_obj_set_flex_grow(label, 1);
    return row;
}

lv_obj_t* createTextArea(lv_obj_t* row, const char* text, uint32_t maxLength, bool password) {
    auto* text_area = lv_textarea_create(row);
    lv_obj_set_width(text_area, LV_PCT(50));
    lv_textarea_set_one_line(text_area, true);
    lv_textarea_set_max_length(text_area, maxLength);
    lv_textarea_set_password_mode(text_area, password);
    lv_textarea_set_text(text_area, text);
    return text_area;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    lv_obj_t* toolbar = lvgl_toolbar_create(parent, "Web Server");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    // Web Server Enable toggle
    ctx->switchWebServerEnabled = lvgl_toolbar_add_switch_action(toolbar);
    if (ctx->wsSettings.webServerEnabled) {
        lv_obj_add_state(ctx->switchWebServerEnabled, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(ctx->switchWebServerEnabled, onWebServerEnabledSwitch, LV_EVENT_VALUE_CHANGED, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(main_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    // Address, while the web server is enabled
    ctx->urlCard = createCard(main_wrapper);
    ctx->labelUrl = lv_label_create(ctx->urlCard);
    lv_label_set_text(ctx->labelUrl, "Web server address");
    ctx->labelUrlValue = lv_label_create(ctx->urlCard);
    updateUrlDisplay(ctx);

    // WiFi mode, with the access point settings in access point mode
    lv_label_set_text(lv_label_create(main_wrapper), "WiFi mode");
    auto* wifi_card = createCard(main_wrapper);
    lv_obj_set_flex_align(wifi_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // The chips are centered while they fit, and scroll when they don't
    ctx->wifiModeChips = lv_obj_create(wifi_card);
    lv_obj_set_size(ctx->wifiModeChips, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(ctx->wifiModeChips, LV_PCT(100), LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(ctx->wifiModeChips, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(ctx->wifiModeChips, LV_DIR_HOR);
    lv_obj_set_style_bg_opa(ctx->wifiModeChips, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ctx->wifiModeChips, 0, LV_STATE_DEFAULT);
    // The chips' margins leave room for their focus rings and space them apart
    lv_obj_set_style_pad_all(ctx->wifiModeChips, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ctx->wifiModeChips, 0, LV_STATE_DEFAULT);
    // In settings::webserver::WiFiMode order
    for (const char* name : { "Station", "Access point" }) {
        auto* chip = lvgl_chip_create(ctx->wifiModeChips);
        lv_label_set_text(lv_label_create(chip), name);
        lv_obj_add_event_cb(chip, onWifiModeChipPressed, LV_EVENT_SHORT_CLICKED, ctx);
    }
    lvgl_grid_navigation_add(ctx->wifiModeChips);

    ctx->apGroup = createGroup(wifi_card);
    auto* ap_open_row = createRow(ctx->apGroup, "Open network");
    ctx->switchApOpenNetwork = lv_switch_create(ap_open_row);
    if (ctx->wsSettings.apOpenNetwork) lv_obj_add_state(ctx->switchApOpenNetwork, LV_STATE_CHECKED);
    lv_obj_add_event_cb(ctx->switchApOpenNetwork, onApOpenNetworkSwitch, LV_EVENT_VALUE_CHANGED, ctx);

    ctx->apPasswordRow = createRow(ctx->apGroup, "Password");
    ctx->textAreaApPassword = createTextArea(ctx->apPasswordRow, ctx->wsSettings.apPassword.c_str(), 64, true);
    lv_obj_add_event_cb(ctx->textAreaApPassword, onApPasswordChanged, LV_EVENT_VALUE_CHANGED, ctx);

    // Authentication, with the credentials while it's required
    auto* auth_row = createRow(main_wrapper, "Require authentication");
    ctx->switchWebServerAuthEnabled = lv_switch_create(auth_row);
    if (ctx->wsSettings.webServerAuthEnabled) lv_obj_add_state(ctx->switchWebServerAuthEnabled, LV_STATE_CHECKED);
    lv_obj_add_event_cb(ctx->switchWebServerAuthEnabled, onWebServerAuthEnabledSwitch, LV_EVENT_VALUE_CHANGED, ctx);

    ctx->authCard = createCard(main_wrapper);
    auto* user_row = createRow(ctx->authCard, "Username");
    ctx->textAreaWebServerUsername = createTextArea(user_row, ctx->wsSettings.webServerUsername.c_str(), 32, false);
    lv_obj_add_event_cb(ctx->textAreaWebServerUsername, onCredentialChanged, LV_EVENT_VALUE_CHANGED, ctx);
    auto* password_row = createRow(ctx->authCard, "Password");
    ctx->textAreaWebServerPassword = createTextArea(password_row, ctx->wsSettings.webServerPassword.c_str(), 64, true);
    lv_obj_add_event_cb(ctx->textAreaWebServerPassword, onCredentialChanged, LV_EVENT_VALUE_CHANGED, ctx);

    // Info text
    auto* info_label = lv_label_create(main_wrapper);
    lv_label_set_long_mode(info_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info_label, LV_PCT(100));
    lv_label_set_text(info_label,
        "WiFi Station credentials are managed separately.\n"
        "Use the WiFi menu to connect to networks.\n\n"
        "AP mode uses the password configured above.");

    updateVisibility(ctx,
        ctx->wsSettings.wifiMode == settings::webserver::WiFiMode::AccessPoint,
        ctx->wsSettings.apOpenNetwork,
        ctx->wsSettings.webServerAuthEnabled,
        ctx->wsSettings.webServerEnabled);
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.wsSettings = settings::webserver::loadOrGetDefault();
    // Reflect the server's actual running state, in case it differs from the persisted setting
    ctx.wsSettings.webServerEnabled = service::webserver::isWebServerEnabled();
    ctx.originalSettings = ctx.wsSettings;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

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
                default:
                    break;
            }
            if (shouldClose) break;
        }
    }

    // Equivalent of the old model's onHide().
    if (ctx.updated) {
        // Read values from text areas - the window (and its widgets) is still alive at this
        // point, since window_manager_remove() below hasn't run yet, but this runs on this
        // app's own thread rather than the LVGL task, so the LVGL lock is needed.
        lvgl_lock();
        if (ctx.textAreaApPassword) {
            ctx.wsSettings.apPassword = lv_textarea_get_text(ctx.textAreaApPassword);
        }
        if (ctx.textAreaWebServerUsername) {
            ctx.wsSettings.webServerUsername = lv_textarea_get_text(ctx.textAreaWebServerUsername);
        }
        if (ctx.textAreaWebServerPassword) {
            ctx.wsSettings.webServerPassword = lv_textarea_get_text(ctx.textAreaWebServerPassword);
        }
        lvgl_unlock();

        // Save to flash only (settings sync at boot handles SD restore)
        // Note: the enable/disable toggle already saved and applied itself immediately
        const auto copy = ctx.wsSettings;
        const bool wifiChanged = ctx.wifiSettingsChanged;

        getMainDispatcher().dispatch([copy, wifiChanged] {
            // Save to flash (fast, low memory pressure)
            if (!settings::webserver::save(copy)) {
                LOG_W(TAG, "Failed to persist WebServer settings; changes may be lost on reboot");
            }

            // Publish event immediately after save so WebServer cache refreshes BEFORE requests arrive
            service::webserver::getPubsub()->publish(service::webserver::WebServerEvent::WebServerSettingsChanged);

            // Only reconnect WiFi if WiFi settings actually changed
            if (wifiChanged) {
                LOG_I(TAG, "WiFi mode changed to %s", copy.wifiMode == settings::webserver::WiFiMode::AccessPoint ? "AP" : "Station");
            }
        });
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.webserversettings",
    .name = "Web Server",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

}
