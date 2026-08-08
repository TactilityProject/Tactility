#ifdef ESP_PLATFORM

#include <Tactility/Tactility.h>
#include <Tactility/settings/WebServerSettings.h>
#include <Tactility/service/webserver/WebServerService.h>
#include <Tactility/service/wifi/Wifi.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/log.h>

#include <lvgl.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

#include <esp_netif.h>
#include <esp_wifi.h>

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
    lv_obj_t* dropdownWifiMode = nullptr;
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
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void onWifiModeChanged(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
    auto index = lv_dropdown_get_selected(dropdown);
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

    if (ctx->textAreaWebServerUsername && ctx->textAreaWebServerPassword) {
        if (enabled) {
            lv_obj_remove_state(ctx->textAreaWebServerUsername, LV_STATE_DISABLED);
            lv_obj_add_flag(ctx->textAreaWebServerUsername, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_remove_state(ctx->textAreaWebServerPassword, LV_STATE_DISABLED);
            lv_obj_add_flag(ctx->textAreaWebServerPassword, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_add_state(ctx->textAreaWebServerUsername, LV_STATE_DISABLED);
            lv_obj_remove_flag(ctx->textAreaWebServerUsername, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_add_state(ctx->textAreaWebServerPassword, LV_STATE_DISABLED);
            lv_obj_remove_flag(ctx->textAreaWebServerPassword, LV_OBJ_FLAG_CLICKABLE);
        }
    }

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

    if (ctx->textAreaApPassword) {
        if (openNetwork) {
            lv_obj_add_state(ctx->textAreaApPassword, LV_STATE_DISABLED);
            lv_obj_remove_flag(ctx->textAreaApPassword, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_remove_state(ctx->textAreaApPassword, LV_STATE_DISABLED);
            lv_obj_add_flag(ctx->textAreaApPassword, LV_OBJ_FLAG_CLICKABLE);
        }
    }

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
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != nullptr) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
                char ip_str[16];
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
                url += ip_str;
            } else {
                url = "Connecting...";
            }
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
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    // WiFi Mode dropdown
    auto* wifi_mode_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(wifi_mode_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(wifi_mode_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(wifi_mode_wrapper, 0, LV_STATE_DEFAULT);
    auto* wifi_mode_label = lv_label_create(wifi_mode_wrapper);
    lv_label_set_text(wifi_mode_label, "WiFi Mode");
    lv_obj_align(wifi_mode_label, LV_ALIGN_LEFT_MID, 0, 0);
    ctx->dropdownWifiMode = lv_dropdown_create(wifi_mode_wrapper);
    lv_obj_align(ctx->dropdownWifiMode, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_options(ctx->dropdownWifiMode, "Station\nAccess Point");
    lv_dropdown_set_selected(ctx->dropdownWifiMode, static_cast<uint32_t>(ctx->wsSettings.wifiMode));
    lv_obj_add_event_cb(ctx->dropdownWifiMode, onWifiModeChanged, LV_EVENT_VALUE_CHANGED, ctx);

    // AP Open Network toggle
    auto* ap_open_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(ap_open_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(ap_open_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ap_open_wrapper, 0, LV_STATE_DEFAULT);
    auto* ap_open_label = lv_label_create(ap_open_wrapper);
    lv_label_set_text(ap_open_label, "AP Open Network");
    lv_obj_align(ap_open_label, LV_ALIGN_LEFT_MID, 0, 0);
    ctx->switchApOpenNetwork = lv_switch_create(ap_open_wrapper);
    if (ctx->wsSettings.apOpenNetwork) lv_obj_add_state(ctx->switchApOpenNetwork, LV_STATE_CHECKED);
    lv_obj_align(ctx->switchApOpenNetwork, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(ctx->switchApOpenNetwork, onApOpenNetworkSwitch, LV_EVENT_VALUE_CHANGED, ctx);

    // AP Password
    auto* ap_pass_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(ap_pass_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(ap_pass_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ap_pass_wrapper, 0, LV_STATE_DEFAULT);
    auto* ap_pass_label = lv_label_create(ap_pass_wrapper);
    lv_label_set_text(ap_pass_label, "AP Password");
    lv_obj_align(ap_pass_label, LV_ALIGN_LEFT_MID, 0, 0);
    ctx->textAreaApPassword = lv_textarea_create(ap_pass_wrapper);
    lv_obj_set_width(ctx->textAreaApPassword, 120);
    lv_obj_align(ctx->textAreaApPassword, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_textarea_set_one_line(ctx->textAreaApPassword, true);
    lv_textarea_set_max_length(ctx->textAreaApPassword, 64);
    lv_textarea_set_password_mode(ctx->textAreaApPassword, true);
    lv_textarea_set_text(ctx->textAreaApPassword, ctx->wsSettings.apPassword.c_str());
    lv_obj_add_event_cb(ctx->textAreaApPassword, onApPasswordChanged, LV_EVENT_VALUE_CHANGED, ctx);
    // Disable password field if open network is enabled
    if (ctx->wsSettings.apOpenNetwork) {
        lv_obj_add_state(ctx->textAreaApPassword, LV_STATE_DISABLED);
        lv_obj_remove_flag(ctx->textAreaApPassword, LV_OBJ_FLAG_CLICKABLE);
    }

    // Web Server Authentication Enable toggle
    auto* ws_auth_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(ws_auth_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(ws_auth_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ws_auth_wrapper, 0, LV_STATE_DEFAULT);
    auto* ws_auth_label = lv_label_create(ws_auth_wrapper);
    lv_label_set_text(ws_auth_label, "Require Authentication");
    lv_obj_align(ws_auth_label, LV_ALIGN_LEFT_MID, 0, 0);
    ctx->switchWebServerAuthEnabled = lv_switch_create(ws_auth_wrapper);
    if (ctx->wsSettings.webServerAuthEnabled) lv_obj_add_state(ctx->switchWebServerAuthEnabled, LV_STATE_CHECKED);
    lv_obj_align(ctx->switchWebServerAuthEnabled, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(ctx->switchWebServerAuthEnabled, onWebServerAuthEnabledSwitch, LV_EVENT_VALUE_CHANGED, ctx);

    // WebServer Username
    auto* ws_user_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(ws_user_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(ws_user_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ws_user_wrapper, 0, LV_STATE_DEFAULT);
    auto* ws_user_label = lv_label_create(ws_user_wrapper);
    lv_label_set_text(ws_user_label, "Username");
    lv_obj_align(ws_user_label, LV_ALIGN_LEFT_MID, 0, 0);
    ctx->textAreaWebServerUsername = lv_textarea_create(ws_user_wrapper);
    if (!ctx->wsSettings.webServerAuthEnabled) {
        lv_obj_add_state(ctx->textAreaWebServerUsername, LV_STATE_DISABLED);
        lv_obj_remove_flag(ctx->textAreaWebServerUsername, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_set_width(ctx->textAreaWebServerUsername, 120);
    lv_obj_align(ctx->textAreaWebServerUsername, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_textarea_set_one_line(ctx->textAreaWebServerUsername, true);
    lv_textarea_set_max_length(ctx->textAreaWebServerUsername, 32);
    lv_textarea_set_text(ctx->textAreaWebServerUsername, ctx->wsSettings.webServerUsername.c_str());
    lv_obj_add_event_cb(ctx->textAreaWebServerUsername, onCredentialChanged, LV_EVENT_VALUE_CHANGED, ctx);

    // WebServer Password
    auto* ws_pass_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(ws_pass_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(ws_pass_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ws_pass_wrapper, 0, LV_STATE_DEFAULT);
    auto* ws_pass_label = lv_label_create(ws_pass_wrapper);
    lv_label_set_text(ws_pass_label, "Password");
    lv_obj_align(ws_pass_label, LV_ALIGN_LEFT_MID, 0, 0);
    ctx->textAreaWebServerPassword = lv_textarea_create(ws_pass_wrapper);
    if (!ctx->wsSettings.webServerAuthEnabled) {
        lv_obj_add_state(ctx->textAreaWebServerPassword, LV_STATE_DISABLED);
        lv_obj_remove_flag(ctx->textAreaWebServerPassword, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_set_width(ctx->textAreaWebServerPassword, 120);
    lv_obj_align(ctx->textAreaWebServerPassword, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_textarea_set_one_line(ctx->textAreaWebServerPassword, true);
    lv_textarea_set_max_length(ctx->textAreaWebServerPassword, 64);
    lv_textarea_set_password_mode(ctx->textAreaWebServerPassword, true);
    lv_textarea_set_text(ctx->textAreaWebServerPassword, ctx->wsSettings.webServerPassword.c_str());
    lv_obj_add_event_cb(ctx->textAreaWebServerPassword, onCredentialChanged, LV_EVENT_VALUE_CHANGED, ctx);

    // URL Display
    auto* url_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(url_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(url_wrapper, 10, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(url_wrapper, 1, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(url_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_cross_place(url_wrapper, LV_FLEX_ALIGN_START, 0);

    ctx->labelUrl = lv_label_create(url_wrapper);
    lv_label_set_text(ctx->labelUrl, "Web Server URL:");

    ctx->labelUrlValue = lv_label_create(url_wrapper);
    if (lv_display_get_color_format(lv_obj_get_display(parent)) == LV_COLOR_FORMAT_L8) {
        lv_obj_set_style_text_color(ctx->labelUrlValue, lv_theme_get_color_secondary(ctx->labelUrlValue), LV_PART_MAIN);
    } else {
        lv_obj_set_style_text_color(ctx->labelUrlValue, lv_palette_main(LV_PALETTE_BLUE), 0);
    }

    updateUrlDisplay(ctx);

    // Info text
    auto* info_label = lv_label_create(main_wrapper);
    lv_label_set_long_mode(info_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info_label, LV_PCT(95));
    if (lv_display_get_color_format(lv_obj_get_display(parent)) != LV_COLOR_FORMAT_L8) {
        lv_obj_set_style_text_color(info_label, lv_palette_main(LV_PALETTE_GREY), 0);
    }
    lv_label_set_text(info_label,
        "WiFi Station credentials are managed separately.\n"
        "Use the WiFi menu to connect to networks.\n\n"
        "AP mode uses the password configured above.");
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.wsSettings = settings::webserver::loadOrGetDefault();
    // Reflect the server's actual running state, in case it differs from the persisted setting
    ctx.wsSettings.webServerEnabled = service::webserver::isWebServerEnabled();
    ctx.originalSettings = ctx.wsSettings;

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
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "WebServerSettings",
    .name = "Web Server",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

}

#endif
