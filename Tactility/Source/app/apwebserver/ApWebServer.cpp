#ifdef ESP_PLATFORM

#include <Tactility/Logger.h>
#include <Tactility/Tactility.h>
#include <Tactility/app/App.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/service/webserver/WebServerService.h>
#include <Tactility/settings/WebServerSettings.h>

#include <lvgl.h>

namespace tt::app::apwebserver {

static const auto LOGGER = tt::Logger("ApWebServerApp");

class ApWebServerApp final : public App {
    lv_obj_t* labelSsidValue = nullptr;
    lv_obj_t* labelPasswordValue = nullptr;
    lv_obj_t* labelIpValue = nullptr;
    
    bool webServerEnabledChanged = false;
    settings::webserver::WebServerSettings wsSettings;

public:
    void onCreate(AppContext& app) override {
        wsSettings = settings::webserver::loadOrGetDefault();
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

        lvgl::toolbar_create(parent, app);

        lv_obj_t* wrapper = lv_obj_create(parent);
        lv_obj_set_width(wrapper, LV_PCT(100));
        lv_obj_set_style_pad_all(wrapper, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_row(wrapper, 4, LV_PART_MAIN);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* labelSsid = lv_label_create(wrapper);
        lv_label_set_text(labelSsid, "SSID:");
        lv_obj_set_style_text_color(labelSsid, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);

        labelSsidValue = lv_label_create(wrapper);
        lv_label_set_long_mode(labelSsidValue, LV_LABEL_LONG_SCROLL);
        lv_obj_set_width(labelSsidValue, LV_PCT(100));
        lv_obj_set_style_margin_hor(labelSsidValue, 2, LV_PART_MAIN);

        lv_obj_t* labelPassword = lv_label_create(wrapper);
        lv_label_set_text(labelPassword, "Pass:");
        lv_obj_set_style_text_color(labelPassword, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);

        labelPasswordValue = lv_label_create(wrapper);
        lv_obj_set_width(labelPasswordValue, LV_PCT(100));
        lv_label_set_long_mode(labelPasswordValue, LV_LABEL_LONG_SCROLL);
        lv_obj_set_style_margin_hor(labelPasswordValue, 2, LV_PART_MAIN);

        lv_obj_t* labelIp = lv_label_create(wrapper);
        lv_label_set_text(labelIp, "IP:");
        lv_obj_set_style_text_color(labelIp, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);

        labelIpValue = lv_label_create(wrapper);
        lv_obj_set_width(labelIpValue, LV_PCT(100));
        lv_label_set_long_mode(labelIpValue, LV_LABEL_LONG_SCROLL);
        lv_obj_set_style_margin_hor(labelIpValue, 2, LV_PART_MAIN);

        // Start AP Mode and WebServer
        settings::webserver::WebServerSettings apSettings = wsSettings;
        apSettings.wifiMode = settings::webserver::WiFiMode::AccessPoint;
        apSettings.webServerEnabled = true;

        if (apSettings.apSsid.empty()) {
            apSettings.apSsid = settings::webserver::generateDefaultApSsid();
        }

        // Generate password if it's an open network or if password is empty
        if (apSettings.apOpenNetwork || apSettings.apPassword.empty()) {
            apSettings.apPassword = settings::webserver::generateRandomCredential(12);
            apSettings.apOpenNetwork = false;
        }

        lv_label_set_text(labelSsidValue, apSettings.apSsid.c_str());
        lv_label_set_text(labelPasswordValue, apSettings.apPassword.c_str());
        lv_label_set_text(labelIpValue, "192.168.4.1");

        // Apply settings and start services
        getMainDispatcher().dispatch([apSettings] {
            if (!settings::webserver::save(apSettings)) {
                LOGGER.error("Failed to save AP settings");
                return;
            }
            service::webserver::getPubsub()->publish(service::webserver::WebServerEvent::WebServerSettingsChanged);
            service::webserver::setWebServerEnabled(true);
        });
        webServerEnabledChanged = true;
    }

    void onHide(AppContext& app) override {
        const auto copy = wsSettings;
        const bool webServerChanged = webServerEnabledChanged;

        getMainDispatcher().dispatch([copy, webServerChanged] {
            if (!settings::webserver::save(copy)) {
                LOGGER.warn("Failed to persist WebServer settings; changes may be lost on reboot");
            }

            service::webserver::getPubsub()->publish(service::webserver::WebServerEvent::WebServerSettingsChanged);

            if (webServerChanged) {
                LOGGER.info("WebServer {}", copy.webServerEnabled ? "enabling..." : "disabling...");
                service::webserver::setWebServerEnabled(copy.webServerEnabled);
            }
        });
    }
};

extern const AppManifest manifest = {
    .appId = "ApWebServer",
    .appName = "AP Web Server",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::Hidden,
    .createApp = create<ApWebServerApp>
};

} // namespace tt::app::apwebserver

#endif
