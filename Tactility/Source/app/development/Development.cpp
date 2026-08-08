#ifdef ESP_PLATFORM

#include <Tactility/Tactility.h>
#include <Tactility/Timer.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/service/development/DevelopmentService.h>
#include <Tactility/service/development/DevelopmentSettings.h>
#include <Tactility/service/wifi/Wifi.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/log.h>

#include <lvgl.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

#include <cstring>

namespace tt::app::development {

constexpr auto* TAG = "Development";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;

    lv_obj_t* enableSwitch = nullptr;
    lv_obj_t* enableOnBootSwitch = nullptr;
    lv_obj_t* statusLabel = nullptr;
    std::shared_ptr<service::development::DevelopmentService> service;
    std::unique_ptr<Timer> timer;
};


void updateViewState(Context* ctx);

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void onEnableSwitchChanged(lv_event_t* event) {
    lv_event_code_t code = lv_event_get_code(event);
    auto* widget = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (code == LV_EVENT_VALUE_CHANGED) {
        bool is_on = lv_obj_has_state(widget, LV_STATE_CHECKED);
        auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
        bool is_changed = is_on != ctx->service->isEnabled();
        if (is_changed) {
            ctx->service->setEnabled(is_on);
        }
    }
}

void onEnableOnBootSwitchChanged(lv_event_t* event) {
    lv_event_code_t code = lv_event_get_code(event);
    auto* widget = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (code == LV_EVENT_VALUE_CHANGED) {
        bool is_on = lv_obj_has_state(widget, LV_STATE_CHECKED);
        bool is_changed = is_on != service::development::shouldEnableOnBoot();
        if (is_changed) {
            // Dispatch it, so file IO doesn't block the UI
            getMainDispatcher().dispatch([is_on] {
                service::development::setEnableOnBoot(is_on);
            });
        }
    }
}

void updateViewState(Context* ctx) {
    if (!ctx->service->isEnabled()) {
        lv_label_set_text(ctx->statusLabel, "Service disabled");
    } else if (service::wifi::getRadioState() != service::wifi::RadioState::ConnectionActive) {
        lv_label_set_text(ctx->statusLabel, "Waiting for connection...");
    } else { // enabled and connected to wifi
        auto ip = service::wifi::getIp();
        if (ip.empty()) {
            lv_label_set_text(ctx->statusLabel, "Waiting for IP...");
        } else {
            const std::string status = std::format("Available at {}", ip);
            lv_label_set_text(ctx->statusLabel, status.c_str());
        }
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    lv_obj_t* toolbar = lvgl_toolbar_create(parent, "Development");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    ctx->enableSwitch = lvgl_toolbar_add_switch_action(toolbar);
    lv_obj_add_event_cb(ctx->enableSwitch, onEnableSwitchChanged, LV_EVENT_VALUE_CHANGED, ctx);

    if (ctx->service->isEnabled()) {
        lv_obj_add_state(ctx->enableSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(ctx->enableSwitch, LV_STATE_CHECKED);
    }

    // Wrappers

    lv_obj_t* content_wrapper = lv_obj_create(parent);
    lv_obj_set_width(content_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(content_wrapper, 1);
    lv_obj_set_flex_flow(content_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_width(content_wrapper, 0, LV_STATE_DEFAULT);
    lvgl::obj_set_style_bg_invisible(content_wrapper);

    // Enable on boot

    lv_obj_t* enable_wrapper = lv_obj_create(content_wrapper);
    lv_obj_set_size(enable_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lvgl::obj_set_style_bg_invisible(enable_wrapper);
    lv_obj_set_style_border_width(enable_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(enable_wrapper, 0, LV_STATE_DEFAULT);

    lv_obj_t* enable_label = lv_label_create(enable_wrapper);
    lv_label_set_text(enable_label, "Enable on boot");
    lv_obj_align(enable_label, LV_ALIGN_LEFT_MID, 0, 0);

    ctx->enableOnBootSwitch = lv_switch_create(enable_wrapper);
    lv_obj_add_event_cb(ctx->enableOnBootSwitch, onEnableOnBootSwitchChanged, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_align(ctx->enableOnBootSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
    if (service::development::shouldEnableOnBoot()) {
        lv_obj_add_state(ctx->enableOnBootSwitch, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(ctx->enableOnBootSwitch, LV_STATE_CHECKED);
    }

    // Status

    ctx->statusLabel = lv_label_create(content_wrapper);

    // Warning

    auto warning_label = lv_label_create(content_wrapper);
    lv_label_set_text(warning_label, "This feature is experimental and uses an unsecured http connection.");
    lv_obj_set_width(warning_label, LV_PCT(100));
    lv_label_set_long_mode(warning_label, LV_LABEL_LONG_WRAP);
    if (lv_display_get_color_format(lv_obj_get_display(parent)) != LV_COLOR_FORMAT_L8) {
        lv_obj_set_style_text_color(warning_label, lv_color_make(0xff, 0xff, 0x00), LV_STATE_DEFAULT);
    }

    updateViewState(ctx);
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.service = service::development::findService();

    if (ctx.service == nullptr) {
        LOG_E(TAG, "Service not found");
        // No window/subscription was ever created - matches the old model, where onCreate()
        // aborting the app meant onShow() was never called either.
        app_manager_finish(appInstanceId);
        return 0;
    }

    ctx.timer = std::make_unique<Timer>(Timer::Type::Periodic, pdMS_TO_TICKS(1000), [&ctx] {
        if (lvgl_is_running()) {
            lvgl_lock();
            updateViewState(&ctx);
            lvgl_unlock();
        }
    });

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);
    ctx.timer->start();

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

    // Equivalent of the old model's onHide(): ensure the periodic update isn't already happening.
    lvgl_lock();
    ctx.timer->stop();
    lvgl_unlock();

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "Development",
    .name = "Development",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace

#endif // ESP_PLATFORM
