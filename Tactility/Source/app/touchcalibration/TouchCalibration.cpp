#include <Tactility/app/touchcalibration/TouchCalibration.h>

#if defined(CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED)

#include <Tactility/Tactility.h>
#include <Tactility/settings/TouchCalibrationSettings.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/log.h>
#include <lvgl/lvgl.h>
#include <lvgl/devices/pointer.h>

#include <algorithm>
#include <lvgl.h>

namespace tt::app::touchcalibration {

constexpr auto* TAG = "TouchCalibration";

extern const ::AppManifest manifest;

namespace {

constexpr int32_t TARGET_MARGIN = 24;

struct Sample {
    uint16_t x;
    uint16_t y;
};

struct Context {
    uint32_t appInstanceId;

    Sample samples[4] = {};
    uint8_t sampleCount = 0;
    bool calibrationApplied = false;

    lv_obj_t* root = nullptr;
    lv_obj_t* target = nullptr;
    lv_obj_t* titleLabel = nullptr;
    lv_obj_t* hintLabel = nullptr;
};


lv_point_t getTargetPoint(uint8_t index, lv_coord_t width, lv_coord_t height) {
    switch (index) {
        case 0:
            return {.x = TARGET_MARGIN, .y = TARGET_MARGIN};
        case 1:
            return {.x = width - TARGET_MARGIN, .y = TARGET_MARGIN};
        case 2:
            return {.x = width - TARGET_MARGIN, .y = height - TARGET_MARGIN};
        default:
            return {.x = TARGET_MARGIN, .y = height - TARGET_MARGIN};
    }
}

void updateUi(Context* ctx) {
    if (ctx->target == nullptr || ctx->root == nullptr || ctx->titleLabel == nullptr || ctx->hintLabel == nullptr) {
        return;
    }

    const auto width = lv_obj_get_content_width(ctx->root);
    const auto height = lv_obj_get_content_height(ctx->root);

    if (ctx->sampleCount < 4) {
        const auto point = getTargetPoint(ctx->sampleCount, width, height);
        lv_obj_set_pos(ctx->target, point.x - 14, point.y - 14);
        lv_label_set_text(ctx->titleLabel, "Touchscreen Calibration");
        lv_label_set_text_fmt(ctx->hintLabel, "Tap target %u/4", static_cast<unsigned>(ctx->sampleCount + 1));
    }
}

// Drives the on-screen outcome text/state; the actual result (Ok/Error) is reported to the
// caller from onPress() below, via ctx->calibrationApplied, once the user taps to dismiss.
void finishCalibration(Context* ctx) {
    const int32_t xLow = (static_cast<int32_t>(ctx->samples[0].x) + static_cast<int32_t>(ctx->samples[3].x)) / 2;
    const int32_t xHigh = (static_cast<int32_t>(ctx->samples[1].x) + static_cast<int32_t>(ctx->samples[2].x)) / 2;
    const int32_t yLow = (static_cast<int32_t>(ctx->samples[0].y) + static_cast<int32_t>(ctx->samples[1].y)) / 2;
    const int32_t yHigh = (static_cast<int32_t>(ctx->samples[2].y) + static_cast<int32_t>(ctx->samples[3].y)) / 2;

    // Targets sit TARGET_MARGIN in from each edge (see getTargetPoint()), not at the screen
    // edges themselves - xLow/xHigh/yLow/yHigh are raw samples at those inset positions, not
    // at 0/width or 0/height. Extrapolate them out to the true edges so the saved range (which
    // lvgl_pointer.h maps onto the full [0, resolution) display range) lines up correctly
    // across the whole screen instead of being off by a margin's worth of scale and offset.
    const auto width = lv_obj_get_content_width(ctx->root);
    const auto height = lv_obj_get_content_height(ctx->root);
    const int32_t xSpan = static_cast<int32_t>(width) - 2 * TARGET_MARGIN;
    const int32_t ySpan = static_cast<int32_t>(height) - 2 * TARGET_MARGIN;

    if (xSpan <= 0 || ySpan <= 0) {
        lv_label_set_text(ctx->titleLabel, "Calibration Failed");
        lv_label_set_text(ctx->hintLabel, "Screen too small. Tap to close.");
        lv_obj_add_flag(ctx->target, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const int32_t xMin = xLow - (xHigh - xLow) * TARGET_MARGIN / xSpan;
    const int32_t xMax = xHigh + (xHigh - xLow) * TARGET_MARGIN / xSpan;
    const int32_t yMin = yLow - (yHigh - yLow) * TARGET_MARGIN / ySpan;
    const int32_t yMax = yHigh + (yHigh - yLow) * TARGET_MARGIN / ySpan;

    settings::touch::TouchCalibrationSettings settings = settings::touch::getDefault();
    settings.enabled = true;
    settings.xMin = xMin;
    settings.xMax = xMax;
    settings.yMin = yMin;
    settings.yMax = yMax;

    if (!settings::touch::isValid(settings)) {
        lv_label_set_text(ctx->titleLabel, "Calibration Failed");
        lv_label_set_text(ctx->hintLabel, "Range invalid. Tap to close.");
        lv_obj_add_flag(ctx->target, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (!settings::touch::save(settings)) {
        lv_label_set_text(ctx->titleLabel, "Calibration Failed");
        lv_label_set_text(ctx->hintLabel, "Unable to save settings. Tap to close.");
        lv_obj_add_flag(ctx->target, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    LvglPointerCalibration calibration = {
        .x_min = xMin,
        .x_max = xMax,
        .y_min = yMin,
        .y_max = yMax,
    };
    lvgl_lock();
    auto* indev = lvgl_pointer_get_default();
    if (indev != nullptr) {
        lvgl_pointer_set_calibration(indev, &calibration);
    }
    lvgl_unlock();
    ctx->calibrationApplied = true;

    LOG_I(TAG, "Saved calibration x=[%d, %d] y=[%d, %d]", xMin, xMax, yMin, yMax);
    lv_label_set_text(ctx->titleLabel, "Calibration Complete");
    lv_label_set_text(ctx->hintLabel, "Touch anywhere to continue.");
    lv_obj_add_flag(ctx->target, LV_OBJ_FLAG_HIDDEN);
}

void onPress(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* indev = lv_event_get_indev(event);
    if (indev == nullptr) {
        return;
    }

    lv_point_t point = {0, 0};
    lv_indev_get_point(indev, &point);

    if (ctx->sampleCount < 4) {
        ctx->samples[ctx->sampleCount] = {
            .x = static_cast<uint16_t>(std::max(static_cast<lv_coord_t>(0), point.x)),
            .y = static_cast<uint16_t>(std::max(static_cast<lv_coord_t>(0), point.y)),
        };
        ctx->sampleCount++;

        if (ctx->sampleCount < 4) {
            updateUi(ctx);
        } else {
            finishCalibration(ctx);
        }
        return;
    }

    // Async, non-blocking - must NOT call app_manager_stop()/app_manager_finish() directly
    // here: this callback runs ON the LVGL task, and app-lifecycle transitions must happen on
    // this app's own thread (woken up via app_event_await() below). The result (Ok/Error) is
    // reported by appMain() itself when it returns, based on ctx.calibrationApplied.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_style_bg_color(parent, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(parent, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(parent, 0, LV_STATE_DEFAULT);

    ctx->root = lv_obj_create(parent);
    lv_obj_set_size(ctx->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(ctx->root, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ctx->root, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ctx->root, 0, LV_STATE_DEFAULT);

    ctx->titleLabel = lv_label_create(ctx->root);
    lv_obj_align(ctx->titleLabel, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_style_text_color(ctx->titleLabel, lv_color_white(), LV_STATE_DEFAULT);
    lv_label_set_text(ctx->titleLabel, "Touchscreen Calibration");

    ctx->hintLabel = lv_label_create(ctx->root);
    lv_obj_align(ctx->hintLabel, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_text_color(ctx->hintLabel, lv_color_white(), LV_STATE_DEFAULT);
    lv_label_set_text(ctx->hintLabel, "Tap target 1/4");

    ctx->target = lv_button_create(ctx->root);
    lv_obj_set_size(ctx->target, 28, 28);
    lv_obj_set_style_radius(ctx->target, LV_RADIUS_CIRCLE, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ctx->target, lv_palette_main(LV_PALETTE_RED), LV_STATE_DEFAULT);
    // Ensure root receives all presses for sampling.
    lv_obj_remove_flag(ctx->target, LV_OBJ_FLAG_CLICKABLE);

    auto* targetLabel = lv_label_create(ctx->target);
    lv_label_set_text(targetLabel, "+");
    lv_obj_center(targetLabel);

    lv_obj_add_flag(ctx->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ctx->root, onPress, LV_EVENT_PRESSED, ctx);

    updateUi(ctx);
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    // Clear any active calibration so the taps sampled below are raw, uncalibrated coordinates.
    lvgl_lock();
    auto* startIndev = lvgl_pointer_get_default();
    if (startIndev != nullptr) {
        lvgl_pointer_set_calibration(startIndev, nullptr);
    }
    lvgl_unlock();

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

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    // finishCalibration() already applied a new calibration on success. On cancel/failure,
    // restore whatever calibration was on disk before the block above cleared it.
    if (!ctx.calibrationApplied) {
        settings::touch::TouchCalibrationSettings settings;
        lvgl_lock();
        auto* endIndev = lvgl_pointer_get_default();
        if (endIndev != nullptr && settings::touch::load(settings) && settings.enabled && settings::touch::isValid(settings)) {
            LvglPointerCalibration calibration = {
                .x_min = settings.xMin,
                .x_max = settings.xMax,
                .y_min = settings.yMin,
                .y_max = settings.yMax,
            };
            lvgl_pointer_set_calibration(endIndev, &calibration);
        }
        lvgl_unlock();
    }

    return ctx.calibrationApplied ? 0 : 2; // Ok : Error
}

} // namespace

uint32_t start(uint32_t callerAppInstanceId) {
    uint32_t instanceId = 0;
    app_manager_start_for_result(manifest.id, callerAppInstanceId, 0, nullptr, &instanceId);
    return instanceId;
}

extern const ::AppManifest manifest = {
    .id = "TouchCalibration",
    .name = "Touch Calibration",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace tt::app::touchcalibration

#endif // defined(CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED)
