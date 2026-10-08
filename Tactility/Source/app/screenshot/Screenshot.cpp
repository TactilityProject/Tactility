#include <Tactility/Tactility.h>
#include <Tactility/TactilityConfig.h>

#if TT_FEATURE_SCREENSHOT_ENABLED

#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/service/screenshot/Screenshot.h>
#include <Tactility/Timer.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/filesystem/fs.h>
#include <tactility/log.h>
#include <tactility/paths.h>

#include <lvgl.h>
#include <lvgl/lvgl.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::screenshot {

constexpr auto* TAG = "Screenshot";

extern const ::AppManifest manifest;

namespace {

enum class CaptureMode {
    Timer,
    AppStart
};

struct ModeOption {
    CaptureMode mode;
    const char* name;
};

constexpr ModeOption MODE_OPTIONS[] = {
    { CaptureMode::Timer, "Timer" },
    { CaptureMode::AppStart, "App start" },
};

struct Context {
    uint32_t appInstanceId;
    CaptureMode mode = CaptureMode::Timer;
    lv_obj_t* modeChips = nullptr;
    lv_obj_t* pathTextArea = nullptr;
    lv_obj_t* startStopButtonLabel = nullptr;
    // Shown in timer mode
    lv_obj_t* delayRow = nullptr;
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

    const uint32_t chip_count = lv_obj_get_child_count(ctx->modeChips);
    for (uint32_t i = 0; i < chip_count; i++) {
        lv_obj_set_state(lv_obj_get_child(ctx->modeChips, static_cast<int32_t>(i)), LV_STATE_CHECKED, MODE_OPTIONS[i].mode == ctx->mode);
    }
    lv_obj_set_flag(ctx->delayRow, LV_OBJ_FLAG_HIDDEN, ctx->mode != CaptureMode::Timer);
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
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
        updateScreenshotMode(ctx);
        return;
    }

    const char* path = lv_textarea_get_text(ctx->pathTextArea);

    error_t result = directory_make(path, true);
    if (result != ERROR_NONE && result != ERROR_ALREADY_EXISTS) {
        LOG_E(TAG, "Task failed to start: couldn't create directory at %s", path);
        return;
    }

    if (ctx->mode == CaptureMode::Timer) {
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

    updateScreenshotMode(ctx);
}

void onModeChipPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    ctx->mode = MODE_OPTIONS[lv_obj_get_index(lv_event_get_target_obj(event))].mode;
    updateScreenshotMode(ctx);
}

/** A transparent row in a card: "Title [content]" */
lv_obj_t* createRow(lv_obj_t* card, const char* title) {
    auto* row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(lv_label_create(row), title);
    return row;
}

void createModeWidgets(Context* ctx, lv_obj_t* parent) {
    auto* title = lv_label_create(parent);
    lv_label_set_text(title, "Mode");
    lv_obj_set_width(title, LV_PCT(100));

    // The chips are centered in the card while they fit, and scroll when they don't
    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ctx->modeChips = lv_obj_create(card);
    lv_obj_set_size(ctx->modeChips, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(ctx->modeChips, LV_PCT(100), LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(ctx->modeChips, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(ctx->modeChips, LV_DIR_HOR);
    lv_obj_set_style_bg_opa(ctx->modeChips, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ctx->modeChips, 0, LV_STATE_DEFAULT);
    // The chips' margins leave room for their focus rings and space them apart
    lv_obj_set_style_pad_all(ctx->modeChips, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ctx->modeChips, 0, LV_STATE_DEFAULT);
    for (const auto& option : MODE_OPTIONS) {
        auto* chip = lvgl_chip_create(ctx->modeChips);
        lv_label_set_text(lv_label_create(chip), option.name);
        lv_obj_add_event_cb(chip, onModeChipPressed, LV_EVENT_SHORT_CLICKED, ctx);
    }
    lvgl_grid_navigation_add(ctx->modeChips);
}

void createSettingsWidgets(Context* ctx, lv_obj_t* parent) {
    auto* card = lvgl_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    auto* path_row = createRow(card, "Path");
    ctx->pathTextArea = lv_textarea_create(path_row);
    lv_textarea_set_one_line(ctx->pathTextArea, true);
    lv_obj_set_flex_grow(ctx->pathTextArea, 1);
    char data_path[FILE_MAX_PATH_STRING_LENGTH];
    if (paths_get_data_path(data_path, sizeof(data_path)) == ERROR_NONE) {
        std::string lvgl_mount_path = std::string(data_path) + "/screenshots";
        lv_textarea_set_text(ctx->pathTextArea, lvgl_mount_path.c_str());
    } else {
        lv_textarea_set_text(ctx->pathTextArea, "Error: no data path");
    }

    ctx->delayRow = createRow(card, "Delay");
    ctx->delayTextArea = lv_textarea_create(ctx->delayRow);
    lv_textarea_set_one_line(ctx->delayTextArea, true);
    lv_textarea_set_accepted_chars(ctx->delayTextArea, "0123456789");
    lv_textarea_set_text(ctx->delayTextArea, "10");
    lv_obj_set_flex_grow(ctx->delayTextArea, 1);
    lv_label_set_text(lv_label_create(ctx->delayRow), "seconds");
}

void createStartStopButton(Context* ctx, lv_obj_t* parent) {
    auto* button = lv_button_create(parent);
    lv_obj_add_event_cb(button, onStartPressed, LV_EVENT_SHORT_CLICKED, ctx);
    ctx->startStopButtonLabel = lv_label_create(button);
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
    // The cards stretch, the start/stop button is centered
    lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    auto service = service::screenshot::optScreenshotService();
    if (service != nullptr && service->getMode() == service::screenshot::Mode::Apps) {
        ctx->mode = CaptureMode::AppStart;
    }

    createModeWidgets(ctx, wrapper);
    createSettingsWidgets(ctx, wrapper);
    createStartStopButton(ctx, wrapper);

    updateScreenshotMode(ctx);

    if (!ctx->updateTimer->isRunning()) {
        ctx->updateTimer->start();
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.updateTimer = std::make_unique<Timer>(Timer::Type::Periodic, 500 / portTICK_PERIOD_MS, [&ctx] {
        if (lvgl_try_lock(500 / portTICK_PERIOD_MS)) {
            updateScreenshotMode(&ctx);
            lvgl_unlock();
        }
    });

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

    if (ctx.updateTimer->isRunning()) {
        ctx.updateTimer->stop();
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.screenshot",
    .name = "Screenshot",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace

#endif
