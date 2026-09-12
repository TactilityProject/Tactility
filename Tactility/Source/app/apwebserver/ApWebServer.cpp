#ifdef ESP_PLATFORM

#include <Tactility/Tactility.h>
#include <Tactility/service/webserver/WebServerService.h>
#include <Tactility/settings/WebServerSettings.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl.h>
#include <lvgl/widgets/toolbar.h>
#include <tactility/check.h>
#include <tactility/log.h>

namespace tt::app::apwebserver {

constexpr auto* TAG = "ApWebServerApp";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    lv_obj_t* labelSsidValue = nullptr;
    lv_obj_t* labelPasswordValue = nullptr;
    lv_obj_t* labelIpValue = nullptr;

    bool webServerEnabledChanged = false;
    settings::webserver::WebServerSettings wsSettings;
};


void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    auto* toolbar = lvgl_toolbar_create(parent, "AP Web Server");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    lv_obj_t* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_style_pad_all(wrapper, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(wrapper, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* labelSsid = lv_label_create(wrapper);
    lv_label_set_text(labelSsid, "SSID:");
    lv_obj_set_style_text_color(labelSsid, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);

    ctx->labelSsidValue = lv_label_create(wrapper);
    lv_obj_set_style_text_align(ctx->labelSsidValue, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(ctx->labelSsidValue, LV_PCT(100));
    lv_label_set_long_mode(ctx->labelSsidValue, LV_LABEL_LONG_SCROLL);
    lv_obj_set_style_margin_hor(ctx->labelSsidValue, 2, LV_PART_MAIN);

    lv_obj_t* labelPassword = lv_label_create(wrapper);
    lv_label_set_text(labelPassword, "Pass:");
    lv_obj_set_style_text_color(labelPassword, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);

    ctx->labelPasswordValue = lv_label_create(wrapper);
    lv_obj_set_style_text_align(ctx->labelPasswordValue, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(ctx->labelPasswordValue, LV_PCT(100));
    lv_label_set_long_mode(ctx->labelPasswordValue, LV_LABEL_LONG_SCROLL);
    lv_obj_set_style_margin_hor(ctx->labelPasswordValue, 2, LV_PART_MAIN);

    lv_obj_t* labelIp = lv_label_create(wrapper);
    lv_label_set_text(labelIp, "IP:");
    lv_obj_set_style_text_color(labelIp, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);

    ctx->labelIpValue = lv_label_create(wrapper);
    lv_obj_set_style_text_align(ctx->labelIpValue, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(ctx->labelIpValue, LV_PCT(100));
    lv_label_set_long_mode(ctx->labelIpValue, LV_LABEL_LONG_SCROLL);
    lv_obj_set_style_margin_hor(ctx->labelIpValue, 2, LV_PART_MAIN);

    // Start AP Mode and WebServer
    settings::webserver::WebServerSettings apSettings = ctx->wsSettings;
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

    lv_label_set_text(ctx->labelSsidValue, apSettings.apSsid.c_str());
    lv_label_set_text(ctx->labelPasswordValue, apSettings.apPassword.c_str());
    lv_label_set_text(ctx->labelIpValue, "192.168.4.1");

    // Apply settings and start services
    getMainDispatcher().dispatch([apSettings] {
        if (!settings::webserver::save(apSettings)) {
            LOG_E(TAG, "Failed to save AP settings");
            return;
        }
        service::webserver::getPubsub()->publish(service::webserver::WebServerEvent::WebServerSettingsChanged);
        service::webserver::setWebServerEnabled(true);
    });
    ctx->webServerEnabledChanged = true;
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.wsSettings = settings::webserver::loadOrGetDefault();

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

    // Equivalent of the old model's onHide(): persist the ORIGINAL settings (as loaded at
    // startup, not the temporary AP-mode config createWidgets() applied above) and revert the
    // web server's enabled state accordingly.
    const auto copy = ctx.wsSettings;
    const bool webServerChanged = ctx.webServerEnabledChanged;

    getMainDispatcher().dispatch([copy, webServerChanged] {
        if (!settings::webserver::save(copy)) {
            LOG_W(TAG, "Failed to persist WebServer settings; changes may be lost on reboot");
        }

        service::webserver::getPubsub()->publish(service::webserver::WebServerEvent::WebServerSettingsChanged);

        if (webServerChanged) {
            LOG_I(TAG, "WebServer %s", copy.webServerEnabled ? "enabling..." : "disabling...");
            service::webserver::setWebServerEnabled(copy.webServerEnabled);
        }
    });

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.apwebserver",
    .name = "AP Web Server",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace tt::app::apwebserver

#endif
