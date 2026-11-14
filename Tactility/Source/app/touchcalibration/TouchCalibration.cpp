#include <Tactility/app/touchcalibration/TouchCalibration.h>

#if defined(CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED)

#include <Tactility/Tactility.h>
#include <Tactility/app/touchcalibration/CalibrationMath.h>
#include <Tactility/lvgl/SystemBars.h>
#include <Tactility/settings/TouchCalibrationSettings.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/start.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/log.h>
#include <lvgl/lvgl.h>
#include <lvgl/devices/pointer.h>
#include <lvgl/theme.h>

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

    // The taps, and the centers of the targets that were shown, in the coordinates of the display
    Sample samples[4] = {};
    lv_point_t targets[4] = {};
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
    // The calibration is applied in the native orientation of the panel, before LVGL rotates the points.
    // The targets are shown, and the taps are reported, in the rotated display, so both go back to native first.
    auto* display = lv_display_get_default();
    const int32_t nativeWidth = lv_display_get_original_horizontal_resolution(display);
    const int32_t nativeHeight = lv_display_get_original_vertical_resolution(display);
    const auto rotation = static_cast<Rotation>(lv_display_get_rotation(display));

    int32_t targetX[4], targetY[4], sampleX[4], sampleY[4];
    for (int i = 0; i < 4; i++) {
        const auto target = toNativePoint({ ctx->targets[i].x, ctx->targets[i].y }, rotation, nativeWidth, nativeHeight);
        const auto sample = toNativePoint({ ctx->samples[i].x, ctx->samples[i].y }, rotation, nativeWidth, nativeHeight);
        targetX[i] = target.x;
        targetY[i] = target.y;
        sampleX[i] = sample.x;
        sampleY[i] = sample.y;
    }

    // The targets are inset from the edges, so the fit extrapolates the raw range out to the true edges of the panel
    int32_t xMin, xMax, yMin, yMax;
    if (!computeRange(targetX, sampleX, 4, nativeWidth - 1, xMin, xMax) ||
        !computeRange(targetY, sampleY, 4, nativeHeight - 1, yMin, yMax)) {
        lv_label_set_text(ctx->titleLabel, "Calibration Failed");
        lv_label_set_text(ctx->hintLabel, "Range invalid. Tap to close.");
        lv_obj_set_hidden(ctx->target, true);
        return;
    }

    settings::touch::TouchCalibrationSettings settings = settings::touch::getDefault();
    settings.enabled = true;
    settings.xMin = xMin;
    settings.xMax = xMax;
    settings.yMin = yMin;
    settings.yMax = yMax;

    if (!settings::touch::isValid(settings)) {
        lv_label_set_text(ctx->titleLabel, "Calibration Failed");
        lv_label_set_text(ctx->hintLabel, "Range invalid. Tap to close.");
        lv_obj_set_hidden(ctx->target, true);
        return;
    }

    if (!settings::touch::save(settings)) {
        lv_label_set_text(ctx->titleLabel, "Calibration Failed");
        lv_label_set_text(ctx->hintLabel, "Unable to save settings. Tap to close.");
        lv_obj_set_hidden(ctx->target, true);
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
    lv_obj_set_hidden(ctx->target, true);
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
        lv_area_t targetArea;
        lv_obj_get_coords(ctx->target, &targetArea);
        ctx->targets[ctx->sampleCount] = { .x = (targetArea.x1 + targetArea.x2) / 2, .y = (targetArea.y1 + targetArea.y2) / 2 };
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

    app_event_emit_close(ctx->appInstanceId);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    lvgl::systemBarsSetStatusbarVisible(false);

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
    // The monochrome theme's primary color is black, which can't be seen on the black background
    lv_obj_set_style_bg_color(ctx->target, lvgl_theme_is_mono() ? lv_color_white() : lv_theme_get_color_primary(ctx->target), LV_STATE_DEFAULT);
    // Ensure root receives all presses for sampling.
    lv_obj_set_clickable(ctx->target, false);

    auto* targetLabel = lv_label_create(ctx->target);
    lv_label_set_text(targetLabel, "+");
    lv_obj_center(targetLabel);

    lv_obj_set_clickable(ctx->root, true);
    lv_obj_add_event_cb(ctx->root, onPress, LV_EVENT_PRESSED, ctx);

    updateUi(ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    lvgl::systemBarsSetStatusbarVisible(true);
    ctx->root = nullptr;
    ctx->target = nullptr;
    ctx->titleLabel = nullptr;
    ctx->hintLabel = nullptr;
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    // Clear any active calibration so the taps sampled below are raw, uncalibrated coordinates.
    lvgl_lock();
    auto* startIndev = lvgl_pointer_get_default();
    if (startIndev != nullptr) {
        lvgl_pointer_set_calibration(startIndev, nullptr);
    }
    lvgl_unlock();

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

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
                default:
                    break;
            }
            if (shouldClose) break;
        }
    }

    window_manager_remove(window);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

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
    AppStartContext context = app_start_context_for_manifest(&manifest);
    app_start_context_set_parent(&context, callerAppInstanceId);
    app_start_with_context(&context, &instanceId);
    return instanceId;
}

extern const ::AppManifest manifest = {
    .id = "tactility.touchcalibration",
    .name = "Touch Calibration",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::touchcalibration

#endif // defined(CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED)
