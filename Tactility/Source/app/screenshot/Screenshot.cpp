#include <Tactility/Tactility.h>
#include <Tactility/TactilityConfig.h>

#if TT_FEATURE_SCREENSHOT_ENABLED

#include <Tactility/Platform.h>
#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/service/screenshot/Screenshot.h>
#include <Tactility/Paths.h>
#include <Tactility/Timer.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/log.h>

#include <lvgl.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::screenshot {

constexpr auto* TAG = "Screenshot";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    lv_obj_t* modeDropdown = nullptr;
    lv_obj_t* pathTextArea = nullptr;
    lv_obj_t* startStopButtonLabel = nullptr;
    lv_obj_t* timerWrapper = nullptr;
    lv_obj_t* delayTextArea = nullptr;
    std::unique_ptr<Timer> updateTimer;
};


void updateScreenshotMode(Context* ctx) {
    auto service = service::screenshot::optScreenshotService();
    if (service == nullptr) {
        LOG_E(TAG, "Service not found/running");
        return;
    }

    lv_obj_t* label = ctx->startStopButtonLabel;
    if (service->isTaskStarted()) {
        lv_label_set_text(label, "Stop");
    } else {
        lv_label_set_text(label, "Start");
    }

    uint32_t selected = lv_dropdown_get_selected(ctx->modeDropdown);
    if (selected == 0) { // Timer
        lv_obj_remove_flag(ctx->timerWrapper, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->timerWrapper, LV_OBJ_FLAG_HIDDEN);
    }
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void onStartPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));

    auto service = service::screenshot::optScreenshotService();
    if (service == nullptr) {
        LOG_E(TAG, "Service not found/running");
        return;
    }

    if (service->isTaskStarted()) {
        LOG_I(TAG, "Stop screenshot");
        service->stop();
    } else {
        uint32_t selected = lv_dropdown_get_selected(ctx->modeDropdown);
        const char* path = lv_textarea_get_text(ctx->pathTextArea);
        if (selected == 0) {
            LOG_I(TAG, "Start timed screenshots");
            const char* delay_text = lv_textarea_get_text(ctx->delayTextArea);
            int delay = atoi(delay_text);
            if (delay > 0) {
                service->startTimed(path, delay, 1);
            } else {
                LOG_W(TAG, "Ignored screenshot start because delay was 0");
            }
        } else {
            LOG_I(TAG, "Start app screenshots");
            service->startApps(path);
        }
    }

    updateScreenshotMode(ctx);
}

void onModeSet(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    updateScreenshotMode(ctx);
}

void createModeSettingWidgets(Context* ctx, lv_obj_t* parent) {
    auto service = service::screenshot::optScreenshotService();
    if (service == nullptr) {
        LOG_E(TAG, "Service not found/running");
        return;
    }

    auto* mode_wrapper = lv_obj_create(parent);
    lv_obj_set_size(mode_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(mode_wrapper, 0, 0);
    lv_obj_set_style_border_width(mode_wrapper, 0, 0);

    auto* mode_label = lv_label_create(mode_wrapper);
    lv_label_set_text(mode_label, "Mode:");
    lv_obj_align(mode_label, LV_ALIGN_LEFT_MID, 0, 0);

    ctx->modeDropdown = lv_dropdown_create(mode_wrapper);
    lv_dropdown_set_options(ctx->modeDropdown, "Timer\nApp start");
    lv_obj_align_to(ctx->modeDropdown, mode_label, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_add_event_cb(ctx->modeDropdown, onModeSet, LV_EVENT_VALUE_CHANGED, ctx);
    service::screenshot::Mode mode = service->getMode();
    if (mode == service::screenshot::Mode::Apps) {
        lv_dropdown_set_selected(ctx->modeDropdown, 1);
    }

    auto* button = lv_button_create(mode_wrapper);
    lv_obj_align(button, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(button, onStartPressed, LV_EVENT_SHORT_CLICKED, ctx);
    ctx->startStopButtonLabel = lv_label_create(button);
    lv_obj_align(ctx->startStopButtonLabel, LV_ALIGN_CENTER, 0, 0);
}

void createFilePathWidgets(Context* ctx, lv_obj_t* parent) {
    auto* path_wrapper = lv_obj_create(parent);
    lv_obj_set_size(path_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(path_wrapper, 0, 0);
    lv_obj_set_style_border_width(path_wrapper, 0, 0);
    lv_obj_set_flex_flow(path_wrapper, LV_FLEX_FLOW_ROW);

    auto* label_wrapper = lv_obj_create(path_wrapper);
    lv_obj_set_style_border_width(label_wrapper, 0, 0);
    lv_obj_set_style_pad_all(label_wrapper, 0, 0);
    lv_obj_set_size(label_wrapper, 44, 36);
    auto* path_label = lv_label_create(label_wrapper);
    lv_label_set_text(path_label, "Path:");
    lv_obj_align(path_label, LV_ALIGN_LEFT_MID, 0, 0);

    ctx->pathTextArea = lv_textarea_create(path_wrapper);
    lv_textarea_set_one_line(ctx->pathTextArea, true);
    lv_obj_set_flex_grow(ctx->pathTextArea, 1);
    if (kernel::getPlatform() == kernel::PlatformEsp) {
        std::string sdcard_path;
        if (findFirstMountedSdCardPath(sdcard_path)) {
            std::string lvgl_mount_path = lvgl::PATH_PREFIX + sdcard_path + "/screenshots";
            lv_textarea_set_text(ctx->pathTextArea, lvgl_mount_path.c_str());
        } else {
            lv_textarea_set_text(ctx->pathTextArea, "Error: no SD card");
        }
    } else { // PC
        lv_textarea_set_text(ctx->pathTextArea, lvgl::PATH_PREFIX);
    }
}

void createTimerSettingsWidgets(Context* ctx, lv_obj_t* parent) {
    ctx->timerWrapper = lv_obj_create(parent);
    lv_obj_set_size(ctx->timerWrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(ctx->timerWrapper, 0, 0);
    lv_obj_set_style_border_width(ctx->timerWrapper, 0, 0);

    auto* delay_wrapper = lv_obj_create(ctx->timerWrapper);
    lv_obj_set_size(delay_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(delay_wrapper, 0, 0);
    lv_obj_set_style_border_width(delay_wrapper, 0, 0);
    lv_obj_set_flex_flow(delay_wrapper, LV_FLEX_FLOW_ROW);

    auto* delay_label_wrapper = lv_obj_create(delay_wrapper);
    lv_obj_set_style_border_width(delay_label_wrapper, 0, 0);
    lv_obj_set_style_pad_all(delay_label_wrapper, 0, 0);
    lv_obj_set_size(delay_label_wrapper, 44, 36);
    auto* delay_label = lv_label_create(delay_label_wrapper);
    lv_label_set_text(delay_label, "Delay:");
    lv_obj_align(delay_label, LV_ALIGN_LEFT_MID, 0, 0);

    ctx->delayTextArea = lv_textarea_create(delay_wrapper);
    lv_textarea_set_one_line(ctx->delayTextArea, true);
    lv_textarea_set_accepted_chars(ctx->delayTextArea, "0123456789");
    lv_textarea_set_text(ctx->delayTextArea, "10");
    lv_obj_set_flex_grow(ctx->delayTextArea, 1);

    auto* delay_unit_label_wrapper = lv_obj_create(delay_wrapper);
    lv_obj_set_style_border_width(delay_unit_label_wrapper, 0, 0);
    lv_obj_set_style_pad_all(delay_unit_label_wrapper, 0, 0);
    lv_obj_set_size(delay_unit_label_wrapper, LV_SIZE_CONTENT, 36);
    auto* delay_unit_label = lv_label_create(delay_unit_label_wrapper);
    lv_obj_align(delay_unit_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_label_set_text(delay_unit_label, "seconds");
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    if (ctx->updateTimer->isRunning()) {
        ctx->updateTimer->stop();
    }

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Screenshot");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

    auto* wrapper = lv_obj_create(parent);
    lv_obj_set_width(wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(wrapper, 1);
    lv_obj_set_style_border_width(wrapper, 0, 0);
    lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);

    createModeSettingWidgets(ctx, wrapper);
    createFilePathWidgets(ctx, wrapper);
    createTimerSettingsWidgets(ctx, wrapper);

    updateScreenshotMode(ctx);

    if (!ctx->updateTimer->isRunning()) {
        ctx->updateTimer->start();
    }
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.updateTimer = std::make_unique<Timer>(Timer::Type::Periodic, 500 / portTICK_PERIOD_MS, [&ctx] {
        if (lvgl_try_lock(500 / portTICK_PERIOD_MS)) {
            updateScreenshotMode(&ctx);
            lvgl_unlock();
        }
    });

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

    if (ctx.updateTimer->isRunning()) {
        ctx.updateTimer->stop();
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "Screenshot",
    .name = "Screenshot",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace

#endif
