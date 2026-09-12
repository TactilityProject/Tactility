#include <Tactility/app/setup/Setup.h>

#include <tactility/paths.h>

#include <Tactility/StringUtils.h>
#include <Tactility/app/timezone/TimeZone.h>
#include <Tactility/app/wifimanage/WifiManage.h>
#include <Tactility/file/File.h>
#include <Tactility/service/wifi/Wifi.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/log.h>

#include <lvgl/fonts.h>
#include <lvgl/lvgl.h>
#include <lvgl.h>

#include <functional>
#include <vector>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#ifdef CONFIG_TT_TOUCH_CALIBRATION_REQUIRED
#include <Tactility/app/touchcalibration/TouchCalibration.h>
#endif

namespace tt::app::setup {

extern const ::AppManifest manifest;

constexpr auto* TAG = "setup";

namespace {

bool getCompletedMarkerPath(std::string& outPath) {
    char path[128];
    if (paths_get_data_path(path, sizeof(path)) != ERROR_NONE) {
        return false;
    }
    outPath = std::string(path) + "/.setup_complete";
    return true;
}

} // namespace

bool isCompleted() {
    std::string path;
    if (!getCompletedMarkerPath(path)) {
        LOG_E(TAG, "Setup path not found");
        return false;
    }
    return file::isFile(path);
}

namespace {

void markCompleted() {
    std::string path;
    if (!getCompletedMarkerPath(path)) {
        return;
    }
    file::writeString(path, "");
}

enum class Phase {
    Welcome,
    StepIntro,
    Done
};

struct StepConfiguration {
    std::string title;
    std::string description;
    std::function<void()> run;
};

struct Context {
    uint32_t appInstanceId;

    Phase phase = Phase::Welcome;
    size_t stepIndex = 0;
    std::vector<StepConfiguration> steps;
    uint32_t pendingStepDialogId = 0;

    lv_obj_t* titleLabel = nullptr;
    lv_obj_t* descriptionLabel = nullptr;
    lv_obj_t* skipButton = nullptr;
    lv_obj_t* continueButton = nullptr;
};


void renderCurrent(Context* ctx) {
    switch (ctx->phase) {
        case Phase::Welcome: {
            lv_label_set_text(ctx->titleLabel, "Welcome");
            auto device_names = string::split(std::string(CONFIG_TT_DEVICE_NAME_SIMPLE), ",");
            lv_label_set_text_fmt(ctx->descriptionLabel, "It's time to set up your %s!", device_names.front().c_str());
            lv_obj_add_flag(ctx->skipButton, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(lv_obj_get_child(ctx->continueButton, 0), "Continue");
            break;
        }
        case Phase::StepIntro: {
            const auto& step = ctx->steps[ctx->stepIndex];
            lv_label_set_text(ctx->titleLabel, step.title.c_str());
            lv_label_set_text(ctx->descriptionLabel, step.description.c_str());
            lv_obj_remove_flag(ctx->skipButton, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(lv_obj_get_child(ctx->skipButton, 0), "Skip");
            lv_label_set_text(lv_obj_get_child(ctx->continueButton, 0), "Continue");
            break;
        }
        case Phase::Done:
            lv_label_set_text(ctx->titleLabel, "Setup Complete");
            lv_label_set_text(ctx->descriptionLabel, "You're all set.");
            lv_obj_add_flag(ctx->skipButton, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(lv_obj_get_child(ctx->continueButton, 0), "Finish");
            break;
    }
}

void advanceTo(Context* ctx, size_t index) {
    if (index < ctx->steps.size()) {
        ctx->stepIndex = index;
        ctx->phase = Phase::StepIntro;
    } else {
        ctx->phase = Phase::Done;
    }

    lvgl_lock();
    renderCurrent(ctx);
    lvgl_unlock();
}

void onSkipClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (ctx->phase == Phase::StepIntro) {
        advanceTo(ctx, ctx->stepIndex + 1);
    }
}

void onContinueClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    switch (ctx->phase) {
        case Phase::Welcome:
            advanceTo(ctx, 0);
            break;
        case Phase::StepIntro:
            ctx->steps[ctx->stepIndex].run();
            break;
        case Phase::Done: {
            markCompleted();
            app_event_emit_close(ctx->appInstanceId);
            break;
        }
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    ctx->titleLabel = lv_label_create(parent);
    lv_obj_set_width(ctx->titleLabel, LV_PCT(80));
    lv_obj_set_style_text_align(ctx->titleLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ctx->titleLabel, LV_LABEL_LONG_WRAP);
    auto* font = lvgl_get_text_font(FONT_SIZE_LARGE);
    lv_obj_set_style_text_font(ctx->titleLabel, font, 0);

    ctx->descriptionLabel = lv_label_create(parent);
    lv_obj_set_width(ctx->descriptionLabel, LV_PCT(80));
    lv_obj_set_style_text_align(ctx->descriptionLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ctx->descriptionLabel, LV_LABEL_LONG_WRAP);
    lv_obj_align(ctx->descriptionLabel, LV_ALIGN_CENTER, 0, 0);

    int title_margin = lvgl_get_text_font_height(FONT_SIZE_LARGE);
    lv_obj_align_to(ctx->titleLabel, ctx->descriptionLabel, LV_ALIGN_OUT_TOP_MID, 0, -title_margin);

    ctx->skipButton = lv_button_create(parent);
    lv_obj_t* skip_label = lv_label_create(ctx->skipButton);
    lv_label_set_text(skip_label, "Skip");
    lv_obj_center(skip_label);
    lv_obj_align(ctx->skipButton, LV_ALIGN_BOTTOM_LEFT, 12, -12);
    lv_obj_add_event_cb(ctx->skipButton, onSkipClicked, LV_EVENT_SHORT_CLICKED, ctx);

    ctx->continueButton = lv_button_create(parent);
    lv_obj_t* continue_label = lv_label_create(ctx->continueButton);
    lv_label_set_text(continue_label, "Continue");
    lv_obj_center(continue_label);
    lv_obj_align(ctx->continueButton, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    lv_obj_add_event_cb(ctx->continueButton, onContinueClicked, LV_EVENT_SHORT_CLICKED, ctx);

    renderCurrent(ctx);
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.steps = {
#if defined(CONFIG_TT_TOUCH_CALIBRATION_REQUIRED)
        {
            .title = "Touch Calibration",
            .description = "Let's calibrate the touch screen.",
            .run = [&ctx] { ctx.pendingStepDialogId = touchcalibration::start(ctx.appInstanceId); }
        },
#endif
        {
            .title = "Time Zone Setup",
            .description = "Let's set the time zone.",
            .run = [&ctx] { ctx.pendingStepDialogId = timezone::start(ctx.appInstanceId, true); }
        },
        {
            .title = "Wi-Fi Setup",
            .description = "Let's connect to a Wi-Fi access point.",
            .run = [&ctx] {
                service::wifi::setEnabled(true);
                ctx.pendingStepDialogId = wifimanage::start(ctx.appInstanceId);
            }
        }
    };

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
                case APP_EVENT_RESULT:
                    if (event.result.launch_id == ctx.pendingStepDialogId) {
                        ctx.pendingStepDialogId = 0;
                        advanceTo(&ctx, ctx.stepIndex + 1);
                    }
                    app_manager_stop(event.result.launch_id);
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

void start() {
    uint32_t instanceId = 0;
    app_start(manifest.id, 0, nullptr, &instanceId);
}

extern const ::AppManifest manifest = {
    .id = "tactility.setup",
    .name = "Setup",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

}
