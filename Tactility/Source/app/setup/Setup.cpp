#include <Tactility/app/setup/Setup.h>
#include <Tactility/app/setup/SetupPrivate.h>

#include <Tactility/lvgl/SystemBars.h>
#include <Tactility/settings/Time.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <gps/gps.h>
#include <location/location.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/device.h>

#include <lvgl/devices/indev.h>
#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/step_indicator.h>
#include <lvgl/widgets/text_button.h>
#include <lvgl.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#ifdef CONFIG_TT_TOUCH_CALIBRATION_REQUIRED
#include <Tactility/app/touchcalibration/TouchCalibration.h>
#endif

namespace tt::app::setup {

extern const ::AppManifest manifest;

namespace {

void advanceTo(Context* ctx, size_t index) {
    if (index < ctx->steps.size()) {
        ctx->stepIndex = index;
        ctx->phase = Phase::StepIntro;
    } else {
        ctx->phase = Phase::Done;
    }

    lvgl_lock();
    if (ctx->phase == Phase::StepIntro) {
        auto& step = ctx->steps[ctx->stepIndex];
        if (step.choice < 0 && step.defaultChoice) {
            step.choice = step.defaultChoice();
        }
    }
    // Covered by a step's app: createWidgets() renders the current page when the window is shown again
    if (ctx->titleLabel != nullptr) {
        renderCurrent(ctx);
    }
    lvgl_unlock();
}

/** Shows the page that a step ends on, so that going back from the next step shows the state it was left in */
void showEndOfStep(Context* ctx, size_t index) {
    auto& step = ctx->steps[index];
    if (step.showLast) {
        ctx->stepIndex = index;
        step.showLast();
    } else if (!step.choiceTitle.empty()) {
        ctx->stepIndex = index;
        ctx->phase = Phase::StepChoice;
        lvgl_lock();
        renderCurrent(ctx);
        lvgl_unlock();
    } else {
        advanceTo(ctx, index);
    }
}

void onBackClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    switch (ctx->phase) {
        case Phase::Welcome:
            break;
        case Phase::StepChoice:
        case Phase::WifiSetup:
            advanceTo(ctx, ctx->stepIndex);
            break;
        case Phase::StepIntro:
            if (ctx->stepIndex == 0) {
                ctx->phase = Phase::Welcome;
                lvgl_lock();
                renderCurrent(ctx);
                lvgl_unlock();
            } else {
                showEndOfStep(ctx, ctx->stepIndex - 1);
            }
            break;
        case Phase::Done:
            showEndOfStep(ctx, ctx->steps.size() - 1);
            break;
    }
}

void onSkipClicked(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (ctx->phase == Phase::StepIntro || ctx->phase == Phase::StepChoice || ctx->phase == Phase::WifiSetup) {
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
            if (ctx->steps[ctx->stepIndex].choiceTitle.empty()) {
                ctx->steps[ctx->stepIndex].run();
            } else {
                ctx->phase = Phase::StepChoice;
                lvgl_lock();
                renderCurrent(ctx);
                lvgl_unlock();
            }
            break;
        case Phase::StepChoice:
            ctx->steps[ctx->stepIndex].run();
            break;
        case Phase::WifiSetup:
            advanceTo(ctx, ctx->stepIndex + 1);
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
    lvgl::systemBarsSetStatusbarVisible(false);

    ctx->content = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->content);
    lv_obj_set_size(ctx->content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_scroll_dir(ctx->content, LV_DIR_VER);
    if (auto* group = lv_group_get_default()) {
        lv_group_add_obj(group, ctx->content);
    }

    // At least as high as the content, so that short pages are centered
    ctx->contentInner = lv_obj_create(ctx->content);
    lv_obj_remove_style_all(ctx->contentInner);
    lv_obj_set_size(ctx->contentInner, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->contentInner, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->contentInner, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ctx->titleLabel = lv_label_create(ctx->contentInner);
    lv_obj_set_width(ctx->titleLabel, LV_PCT(80));
    lv_obj_set_style_text_align(ctx->titleLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ctx->titleLabel, LV_LABEL_LONG_WRAP);
    auto* font = lvgl_get_text_font(getTitleFontSize());
    lv_obj_set_style_text_font(ctx->titleLabel, font, 0);

    ctx->descriptionLabel = lv_label_create(ctx->contentInner);
    lv_obj_set_width(ctx->descriptionLabel, LV_PCT(80));
    lv_obj_set_style_text_align(ctx->descriptionLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ctx->descriptionLabel, LV_LABEL_LONG_WRAP);

    // The choices of a step: one can be picked, and they wrap to more lines on narrow screens
    ctx->choiceChips = lv_obj_create(ctx->contentInner);
    lv_obj_remove_style_all(ctx->choiceChips);
    lv_obj_set_size(ctx->choiceChips, LV_PCT(100), LV_SIZE_CONTENT);
    // The chip margins space the choices, so they fit on one line on small screens
    lv_obj_set_flex_flow(ctx->choiceChips, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ctx->choiceChips, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lvgl_grid_navigation_add(ctx->choiceChips);
    lv_obj_set_hidden(ctx->choiceChips, true);

    // Filled with the time zone list on the time zone page
    ctx->timeZoneContainer = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->timeZoneContainer);
    lv_obj_set_flex_flow(ctx->timeZoneContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(ctx->timeZoneContainer, LV_DPX(8), LV_STATE_DEFAULT);
    lv_obj_set_hidden(ctx->timeZoneContainer, true);

    // Filled with the network list on the Wi-Fi page
    ctx->wifiContainer = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->wifiContainer);
    lv_obj_set_flex_flow(ctx->wifiContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(ctx->wifiContainer, LV_DPX(8), LV_STATE_DEFAULT);
    lv_obj_set_hidden(ctx->wifiContainer, true);

    // The buttons, with the progress through the pages below them at the bottom
    auto* bottom = lv_obj_create(parent);
    ctx->bottom = bottom;
    lv_obj_remove_style_all(bottom);
    lv_obj_set_size(bottom, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Back on the left, and Skip next to Continue on the right
    auto* buttonRow = lv_obj_create(bottom);
    lv_obj_remove_style_all(buttonRow);
    lv_obj_set_size(buttonRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(buttonRow, LV_DPX(8), LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(buttonRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttonRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ctx->backButton = lvgl_text_button_create(buttonRow);
    lv_label_set_text(lv_label_create(ctx->backButton), "Back");
    lv_obj_add_event_cb(ctx->backButton, onBackClicked, LV_EVENT_SHORT_CLICKED, ctx);

    auto* spacer = lv_obj_create(buttonRow);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_height(spacer, 1);
    lv_obj_set_flex_grow(spacer, 1);
    // Grid navigation would otherwise stop on it
    lv_obj_set_clickable(spacer, false);

    ctx->skipButton = lvgl_text_button_create(buttonRow);
    lv_label_set_text(lv_label_create(ctx->skipButton), "Skip");
    lv_obj_add_event_cb(ctx->skipButton, onSkipClicked, LV_EVENT_SHORT_CLICKED, ctx);

    ctx->continueButton = lv_button_create(buttonRow);
    lv_label_set_text(lv_label_create(ctx->continueButton), "Continue");
    lv_obj_add_event_cb(ctx->continueButton, onContinueClicked, LV_EVENT_SHORT_CLICKED, ctx);

    ctx->stepIndicator = lvgl_step_indicator_create(bottom);
    lvgl_step_indicator_set_step_count(ctx->stepIndicator, getStepCount(ctx));

    // The arrow keys move between the buttons, and Continue has the focus first
    lvgl_grid_navigation_add(buttonRow);
    if (!lvgl_indev_exists(LV_INDEV_TYPE_POINTER)) {
        lv_group_focus_obj(buttonRow);
        lv_gridnav_set_focused(buttonRow, ctx->continueButton, LV_ANIM_OFF);
    }

    renderCurrent(ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    lvgl::systemBarsSetStatusbarVisible(true);
    ctx->content = nullptr;
    ctx->contentInner = nullptr;
    ctx->titleLabel = nullptr;
    ctx->descriptionLabel = nullptr;
    ctx->backButton = nullptr;
    ctx->skipButton = nullptr;
    ctx->continueButton = nullptr;
    ctx->stepIndicator = nullptr;
    ctx->bottom = nullptr;
    ctx->timeZoneContainer = nullptr;
    ctx->choiceChips = nullptr;
    ctx->wifiContainer = nullptr;
    ctx->timeZoneView->destroyWidgets();
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
            .title = "Wi-Fi Setup",
            .description = "Let's connect to a Wi-Fi access point.",
            .run = [&ctx] { enterWifiSetup(&ctx); },
            .showLast = [&ctx] { enterWifiSetup(&ctx); }
        }
    };

    const size_t locationStepIndex = ctx.steps.size();
    ctx.steps.push_back({
        .title = "Location Services",
        .description = "Allow apps to retrieve your location. It uses GPS and/or contacts geoiplookup.io",
        .run = [&ctx] {
            // The order of the choices: precise, approximate, disabled
            const int32_t choice = ctx.steps[ctx.stepIndex].choice;
            if (choice == 0 || choice == 1) {
                location_set_precision(choice == 0 ? LOCATION_PRECISION_PRECISE : LOCATION_PRECISION_APPROXIMATE);
                location_set_enabled(true);
            } else {
                location_set_enabled(false);
            }
            advanceTo(&ctx, ctx.stepIndex + 1);
        },
        .content = StepConfiguration::Content::Choice,
        .choiceTitle = "App location access:",
        .choices = { "Precise", "Approximate", "Disabled" }
    });

    // Setting the time zone also sets the radio region, as both depend on where the device is
    ctx.steps.push_back({
        .title = "Time Zone",
        .description = "",
        .run = [&ctx] {
            settings::setTimeZone(ctx.timeZoneName, ctx.timeZoneCode);
            advanceTo(&ctx, ctx.stepIndex + 1);
        },
        .content = StepConfiguration::Content::TimeZones
    });
    ctx.timeZoneView = std::make_unique<timezone::TimeZoneView>([&ctx](const std::string& name, const std::string& code) {
        ctx.timeZoneName = name;
        ctx.timeZoneCode = code;
        if (ctx.continueButton != nullptr) {
            lv_obj_remove_state(ctx.continueButton, LV_STATE_DISABLED);
        }
    });

    if (device_exists_of_type(&GPS_TYPE)) {
        ctx.steps.push_back({
            .title = "Time from GPS",
            .description = "Set time from GPS at boot?",
            .run = [&ctx] {
                // The order of the choices: enabled, disabled
                settings::setGpsTimeSyncAtBoot(ctx.steps[ctx.stepIndex].choice == 0);
                advanceTo(&ctx, ctx.stepIndex + 1);
            },
            .content = StepConfiguration::Content::Choice,
            .choices = { "Enabled", "Disabled" },
            // Enabled unless location services are, as the time needs them
            .defaultChoice = [&ctx, locationStepIndex] {
                const int32_t locationChoice = ctx.steps[locationStepIndex].choice;
                return (locationChoice == 0 || locationChoice == 1) ? 0 : 1;
            }
        });
    }

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    wifiStart(&ctx, &event_group);

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

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
                        const auto& onResult = ctx.steps[ctx.stepIndex].onResult;
                        if (onResult != nullptr) {
                            onResult(event.result.result);
                        }
                        advanceTo(&ctx, ctx.stepIndex + 1);
                    }
                    app_manager_stop(event.result.launch_id);
                    break;
                default:
                    break;
            }
            if (shouldClose) break;
        }

        wifiProcess(&ctx);
    }

    wifiStop(&ctx);

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

void start() {
    uint32_t instanceId = 0;
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_with_context(&context, &instanceId);
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

