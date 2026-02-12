#ifdef ESP_PLATFORM

#include <Tactility/Tactility.h>
#include <Tactility/settings/WebServerSettings.h>
#include <Tactility/service/wifi/Wifi.h>
#include <Tactility/service/wifi/WifiApSettings.h>
#include <Tactility/service/webserver/AssetVersion.h>
#include <Tactility/service/webserver/WebServerService.h>
#include <Tactility/Assets.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/Logger.h>

#include <lvgl.h>
#include <tactility/lvgl_symbols_shared.h>

#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

namespace tt::app::webserversettings {

static const auto LOGGER = tt::Logger("WebServerSettingsApp");

class WebServerSettingsApp final : public App {

    settings::webserver::WebServerSettings wsSettings;
    settings::webserver::WebServerSettings originalSettings;
    bool updated = false;
    bool wifiSettingsChanged = false;
    bool webServerEnabledChanged = false;
    lv_obj_t* dropdownWifiMode = nullptr;
    lv_obj_t* textAreaApPassword = nullptr;
    lv_obj_t* switchApOpenNetwork = nullptr;
    lv_obj_t* switchWebServerEnabled = nullptr;
    lv_obj_t* switchWebServerAuthEnabled = nullptr;
    lv_obj_t* textAreaWebServerUsername = nullptr;
    lv_obj_t* textAreaWebServerPassword = nullptr;
    lv_obj_t* labelUrl = nullptr;
    lv_obj_t* labelUrlValue = nullptr;

    static void onWifiModeChanged(lv_event_t* e) {
        auto* app = static_cast<WebServerSettingsApp*>(lv_event_get_user_data(e));
        auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(e));
        auto index = lv_dropdown_get_selected(dropdown);
        getMainDispatcher().dispatch([app, index] {
            app->wsSettings.wifiMode = static_cast<settings::webserver::WiFiMode>(index);
            app->updated = true;
            app->wifiSettingsChanged = true;
            if (lvgl::lock(100)) {
                app->updateUrlDisplay();
                lvgl::unlock();
            }
        });
    }

    static void onWebServerEnabledSwitch(lv_event_t* e) {
        auto* app = static_cast<WebServerSettingsApp*>(lv_event_get_user_data(e));
        bool enabled = lv_obj_has_state(app->switchWebServerEnabled, LV_STATE_CHECKED);
        getMainDispatcher().dispatch([app, enabled] {
            app->wsSettings.webServerEnabled = enabled;
            app->updated = true;
            app->webServerEnabledChanged = true;
            if (lvgl::lock(100)) {
                app->updateUrlDisplay();
                lvgl::unlock();
            }
        });
    }

    static void onWebServerAuthEnabledSwitch(lv_event_t* e) {
        auto* app = static_cast<WebServerSettingsApp*>(lv_event_get_user_data(e));
        bool enabled = lv_obj_has_state(app->switchWebServerAuthEnabled, LV_STATE_CHECKED);

        if (app->textAreaWebServerUsername && app->textAreaWebServerPassword) {
            if (enabled) {
                lv_obj_remove_state(app->textAreaWebServerUsername, LV_STATE_DISABLED);
                lv_obj_add_flag(app->textAreaWebServerUsername, LV_OBJ_FLAG_CLICKABLE);

                lv_obj_remove_state(app->textAreaWebServerPassword, LV_STATE_DISABLED);
                lv_obj_add_flag(app->textAreaWebServerPassword, LV_OBJ_FLAG_CLICKABLE);
            } else {
                lv_obj_add_state(app->textAreaWebServerUsername, LV_STATE_DISABLED);
                lv_obj_remove_flag(app->textAreaWebServerUsername, LV_OBJ_FLAG_CLICKABLE);

                lv_obj_add_state(app->textAreaWebServerPassword, LV_STATE_DISABLED);
                lv_obj_remove_flag(app->textAreaWebServerPassword, LV_OBJ_FLAG_CLICKABLE);
            }
        }

        getMainDispatcher().dispatch([app, enabled] {
            app->wsSettings.webServerAuthEnabled = enabled;
            app->updated = true;
        });
    }

    static void onCredentialChanged(lv_event_t* e) {
        auto* app = static_cast<WebServerSettingsApp*>(lv_event_get_user_data(e));
        getMainDispatcher().dispatch([app] {
            app->updated = true;
        });
    }

    static void onApPasswordChanged(lv_event_t* e) {
        auto* app = static_cast<WebServerSettingsApp*>(lv_event_get_user_data(e));
        getMainDispatcher().dispatch([app] {
            app->updated = true;
            app->wifiSettingsChanged = true;
        });
    }

    static void onApOpenNetworkSwitch(lv_event_t* e) {
        auto* app = static_cast<WebServerSettingsApp*>(lv_event_get_user_data(e));
        bool openNetwork = lv_obj_has_state(app->switchApOpenNetwork, LV_STATE_CHECKED);

        if (app->textAreaApPassword) {
            if (openNetwork) {
                lv_obj_add_state(app->textAreaApPassword, LV_STATE_DISABLED);
                lv_obj_remove_flag(app->textAreaApPassword, LV_OBJ_FLAG_CLICKABLE);
            } else {
                lv_obj_remove_state(app->textAreaApPassword, LV_STATE_DISABLED);
                lv_obj_add_flag(app->textAreaApPassword, LV_OBJ_FLAG_CLICKABLE);
            }
        }

        getMainDispatcher().dispatch([app, openNetwork] {
            app->wsSettings.apOpenNetwork = openNetwork;
            app->updated = true;
            app->wifiSettingsChanged = true;
        });
    }

    static void onSyncAssets(lv_event_t* e) {
        auto* app = static_cast<WebServerSettingsApp*>(lv_event_get_user_data(e));
        auto* btn = static_cast<lv_obj_t*>(lv_event_get_target_obj(e));
        lv_obj_add_state(btn, LV_STATE_DISABLED);
        LOGGER.info("Manual asset sync triggered");

        getMainDispatcher().dispatch([app, btn]{
            bool success = service::webserver::syncAssets();
            if (success) {
                LOGGER.info("Asset sync completed successfully");
            } else {
                LOGGER.error("Asset sync failed");
            }
            // Only re-enable if button still exists (user hasn't navigated away)
            // Must acquire LVGL lock since we're not in an LVGL event callback context
            if (lvgl::lock(1000)) {
                if (lv_obj_is_valid(btn)) {
                    lv_obj_remove_state(btn, LV_STATE_DISABLED);
                }
                lvgl::unlock();
            }
        });
    }

    void updateUrlDisplay() {
        if (!labelUrlValue) return;

        if (!wsSettings.webServerEnabled) {
            lv_label_set_text(labelUrlValue, "Disabled");
            return;
        }

        std::string url = "http://";
        
        if (wsSettings.wifiMode == settings::webserver::WiFiMode::AccessPoint) {
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
            if (wsSettings.webServerPort != 80) {
                url += ":" + std::to_string(wsSettings.webServerPort);
            }
        }

        lv_label_set_text(labelUrlValue, url.c_str());
    }

public:
    void onCreate(AppContext& app) override {
        wsSettings = settings::webserver::loadOrGetDefault();
        originalSettings = wsSettings;
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lv_obj_t* toolbar = lvgl::toolbar_create(parent, app);

        // Web Server Enable toggle
        switchWebServerEnabled = lvgl::toolbar_add_switch_action(toolbar);
        if (wsSettings.webServerEnabled) {
            lv_obj_add_state(switchWebServerEnabled, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(switchWebServerEnabled, onWebServerEnabledSwitch, LV_EVENT_VALUE_CHANGED, this);

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
        dropdownWifiMode = lv_dropdown_create(wifi_mode_wrapper);
        lv_obj_align(dropdownWifiMode, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_dropdown_set_options(dropdownWifiMode, "Station\nAccess Point");
        lv_dropdown_set_selected(dropdownWifiMode, static_cast<uint32_t>(wsSettings.wifiMode));
        lv_obj_add_event_cb(dropdownWifiMode, onWifiModeChanged, LV_EVENT_VALUE_CHANGED, this);

        // AP Open Network toggle
        auto* ap_open_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(ap_open_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(ap_open_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ap_open_wrapper, 0, LV_STATE_DEFAULT);
        auto* ap_open_label = lv_label_create(ap_open_wrapper);
        lv_label_set_text(ap_open_label, "AP Open Network");
        lv_obj_align(ap_open_label, LV_ALIGN_LEFT_MID, 0, 0);
        switchApOpenNetwork = lv_switch_create(ap_open_wrapper);
        if (wsSettings.apOpenNetwork) lv_obj_add_state(switchApOpenNetwork, LV_STATE_CHECKED);
        lv_obj_align(switchApOpenNetwork, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(switchApOpenNetwork, onApOpenNetworkSwitch, LV_EVENT_VALUE_CHANGED, this);

        // AP Password
        auto* ap_pass_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(ap_pass_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(ap_pass_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ap_pass_wrapper, 0, LV_STATE_DEFAULT);
        auto* ap_pass_label = lv_label_create(ap_pass_wrapper);
        lv_label_set_text(ap_pass_label, "AP Password");
        lv_obj_align(ap_pass_label, LV_ALIGN_LEFT_MID, 0, 0);
        textAreaApPassword = lv_textarea_create(ap_pass_wrapper);
        lv_obj_set_width(textAreaApPassword, 120);
        lv_obj_align(textAreaApPassword, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_textarea_set_one_line(textAreaApPassword, true);
        lv_textarea_set_max_length(textAreaApPassword, 64);
        lv_textarea_set_password_mode(textAreaApPassword, true);
        lv_textarea_set_text(textAreaApPassword, wsSettings.apPassword.c_str());
        lv_obj_add_event_cb(textAreaApPassword, onApPasswordChanged, LV_EVENT_VALUE_CHANGED, this);
        // Disable password field if open network is enabled
        if (wsSettings.apOpenNetwork) {
            lv_obj_add_state(textAreaApPassword, LV_STATE_DISABLED);
            lv_obj_remove_flag(textAreaApPassword, LV_OBJ_FLAG_CLICKABLE);
        }

        // Web Server Authentication Enable toggle
        auto* ws_auth_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(ws_auth_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(ws_auth_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ws_auth_wrapper, 0, LV_STATE_DEFAULT);
        auto* ws_auth_label = lv_label_create(ws_auth_wrapper);
        lv_label_set_text(ws_auth_label, "Require Authentication");
        lv_obj_align(ws_auth_label, LV_ALIGN_LEFT_MID, 0, 0);
        switchWebServerAuthEnabled = lv_switch_create(ws_auth_wrapper);
        if (wsSettings.webServerAuthEnabled) lv_obj_add_state(switchWebServerAuthEnabled, LV_STATE_CHECKED);
        lv_obj_align(switchWebServerAuthEnabled, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(switchWebServerAuthEnabled, onWebServerAuthEnabledSwitch, LV_EVENT_VALUE_CHANGED, this);

        // WebServer Username
        auto* ws_user_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(ws_user_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(ws_user_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ws_user_wrapper, 0, LV_STATE_DEFAULT);
        auto* ws_user_label = lv_label_create(ws_user_wrapper);
        lv_label_set_text(ws_user_label, "Username");
        lv_obj_align(ws_user_label, LV_ALIGN_LEFT_MID, 0, 0);
        textAreaWebServerUsername = lv_textarea_create(ws_user_wrapper);
        if (!wsSettings.webServerAuthEnabled) {
            lv_obj_add_state(textAreaWebServerUsername, LV_STATE_DISABLED);
            lv_obj_remove_flag(textAreaWebServerUsername, LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_set_width(textAreaWebServerUsername, 120);
        lv_obj_align(textAreaWebServerUsername, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_textarea_set_one_line(textAreaWebServerUsername, true);
        lv_textarea_set_max_length(textAreaWebServerUsername, 32);
        lv_textarea_set_text(textAreaWebServerUsername, wsSettings.webServerUsername.c_str());
        lv_obj_add_event_cb(textAreaWebServerUsername, onCredentialChanged, LV_EVENT_VALUE_CHANGED, this);

        // WebServer Password
        auto* ws_pass_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(ws_pass_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(ws_pass_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ws_pass_wrapper, 0, LV_STATE_DEFAULT);
        auto* ws_pass_label = lv_label_create(ws_pass_wrapper);
        lv_label_set_text(ws_pass_label, "Password");
        lv_obj_align(ws_pass_label, LV_ALIGN_LEFT_MID, 0, 0);
        textAreaWebServerPassword = lv_textarea_create(ws_pass_wrapper);
        if (!wsSettings.webServerAuthEnabled) {
            lv_obj_add_state(textAreaWebServerPassword, LV_STATE_DISABLED);
            lv_obj_remove_flag(textAreaWebServerPassword, LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_set_width(textAreaWebServerPassword, 120);
        lv_obj_align(textAreaWebServerPassword, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_textarea_set_one_line(textAreaWebServerPassword, true);
        lv_textarea_set_max_length(textAreaWebServerPassword, 64);
        lv_textarea_set_password_mode(textAreaWebServerPassword, true);
        lv_textarea_set_text(textAreaWebServerPassword, wsSettings.webServerPassword.c_str());
        lv_obj_add_event_cb(textAreaWebServerPassword, onCredentialChanged, LV_EVENT_VALUE_CHANGED, this);

        // URL Display
        auto* url_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(url_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(url_wrapper, 10, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(url_wrapper, 1, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(url_wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_flex_cross_place(url_wrapper, LV_FLEX_ALIGN_START, 0);
        
        labelUrl = lv_label_create(url_wrapper);
        lv_label_set_text(labelUrl, "Web Server URL:");
        
        labelUrlValue = lv_label_create(url_wrapper);
        lv_obj_set_style_text_color(labelUrlValue, lv_palette_main(LV_PALETTE_BLUE), 0);

        updateUrlDisplay();

        // Sync Assets button
        auto* sync_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(sync_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(sync_wrapper, 10, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(sync_wrapper, 1, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(sync_wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_flex_cross_place(sync_wrapper, LV_FLEX_ALIGN_START, 0);
        
        auto* sync_label = lv_label_create(sync_wrapper);
        lv_label_set_text(sync_label, "Asset Synchronization");
        lv_obj_set_style_text_font(sync_label, &lv_font_montserrat_14, 0);
        
        auto* sync_info = lv_label_create(sync_wrapper);
        lv_label_set_long_mode(sync_info, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(sync_info, LV_PCT(95));
        lv_obj_set_style_text_color(sync_info, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(sync_info, "Sync web assets between Data partition and SD card backup");
        
        auto* sync_button = lv_btn_create(sync_wrapper);
        lv_obj_set_width(sync_button, LV_SIZE_CONTENT);
        auto* sync_button_label = lv_label_create(sync_button);
        lv_label_set_text(sync_button_label, "Sync Assets Now");
        lv_obj_center(sync_button_label);
        lv_obj_add_event_cb(sync_button, onSyncAssets, LV_EVENT_CLICKED, this);

        // Info text
        auto* info_label = lv_label_create(main_wrapper);
        lv_label_set_long_mode(info_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(info_label, LV_PCT(95));
        lv_obj_set_style_text_color(info_label, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_label_set_text(info_label,
            "WiFi Station credentials are managed separately.\n"
            "Use the WiFi menu to connect to networks.\n\n"
            "AP mode uses the password configured above.");
    }

    void onHide(AppContext& app) override {
        if (updated) {
            // Read values from text areas
            if (textAreaApPassword) {
                wsSettings.apPassword = lv_textarea_get_text(textAreaApPassword);
            }
            if (textAreaWebServerUsername) {
                wsSettings.webServerUsername = lv_textarea_get_text(textAreaWebServerUsername);
            }
            if (textAreaWebServerPassword) {
                wsSettings.webServerPassword = lv_textarea_get_text(textAreaWebServerPassword);
            }

            // Save to flash only (settings sync at boot handles SD restore)
            const auto copy = wsSettings;
            const bool wifiChanged = wifiSettingsChanged;
            const bool webServerChanged = webServerEnabledChanged;

            getMainDispatcher().dispatch([copy, wifiChanged, webServerChanged]{
                // Save to flash (fast, low memory pressure)
                if (!settings::webserver::save(copy)) {
                    LOGGER.warn("Failed to persist WebServer settings; changes may be lost on reboot");
                }

                // Publish event immediately after save so WebServer cache refreshes BEFORE requests arrive
                service::webserver::getPubsub()->publish(service::webserver::WebServerEvent::WebServerSettingsChanged);

                // Only reconnect WiFi if WiFi settings actually changed
                if (wifiChanged) {
                    LOGGER.info("WiFi mode changed to {}", copy.wifiMode == settings::webserver::WiFiMode::AccessPoint ? "AP" : "Station");
                }

                // Control WebServer service immediately
                if (webServerChanged) {
                    LOGGER.info("WebServer {}", copy.webServerEnabled ? "enabling..." : "disabling...");
                    service::webserver::setWebServerEnabled(copy.webServerEnabled);
                }
            });
        }
    }
};

extern const AppManifest manifest = {
    .appId = "WebServerSettings",
    .appName = "Web Server",
    .appIcon = LVGL_SYMBOL_CLOUD,
    .appCategory = Category::System,
    .createApp = create<WebServerSettingsApp>
};

}

#endif
