#include <Tactility/app/touchcalibration/TouchCalibration.h>

#if defined(CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED)

#include <Tactility/Tactility.h>
#include <Tactility/settings/TouchCalibrationSettings.h>

#include <tactility/log.h>
#include <tactility/lvgl_module.h>
#include <tactility/lvgl_pointer.h>

#include <algorithm>
#include <lvgl.h>

namespace tt::app::touchcalibration {

constexpr auto* TAG = "TouchCalibration";

extern const AppManifest manifest;

LaunchId start() {
    return app::start(manifest.appId);
}

class TouchCalibrationApp final : public App {

    static constexpr int32_t TARGET_MARGIN = 24;

    struct Sample {
        uint16_t x;
        uint16_t y;
    };

    Sample samples[4] = {};
    uint8_t sampleCount = 0;
    bool calibrationApplied = false;

    lv_obj_t* root = nullptr;
    lv_obj_t* target = nullptr;
    lv_obj_t* titleLabel = nullptr;
    lv_obj_t* hintLabel = nullptr;

    static void onPress(lv_event_t* event) {
        auto* self = static_cast<TouchCalibrationApp*>(lv_event_get_user_data(event));
        if (self != nullptr) {
            self->onPressInternal(event);
        }
    }

    static lv_point_t getTargetPoint(uint8_t index, lv_coord_t width, lv_coord_t height) {
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

    void updateUi() {
        if (target == nullptr || root == nullptr || titleLabel == nullptr || hintLabel == nullptr) {
            return;
        }

        const auto width = lv_obj_get_content_width(root);
        const auto height = lv_obj_get_content_height(root);

        if (sampleCount < 4) {
            const auto point = getTargetPoint(sampleCount, width, height);
            lv_obj_set_pos(target, point.x - 14, point.y - 14);
            lv_label_set_text(titleLabel, "Touchscreen Calibration");
            lv_label_set_text_fmt(hintLabel, "Tap target %u/4", static_cast<unsigned>(sampleCount + 1));
        }
    }

    void finishCalibration() {
        const int32_t xLow = (static_cast<int32_t>(samples[0].x) + static_cast<int32_t>(samples[3].x)) / 2;
        const int32_t xHigh = (static_cast<int32_t>(samples[1].x) + static_cast<int32_t>(samples[2].x)) / 2;
        const int32_t yLow = (static_cast<int32_t>(samples[0].y) + static_cast<int32_t>(samples[1].y)) / 2;
        const int32_t yHigh = (static_cast<int32_t>(samples[2].y) + static_cast<int32_t>(samples[3].y)) / 2;

        // Targets sit TARGET_MARGIN in from each edge (see getTargetPoint()), not at the screen
        // edges themselves - xLow/xHigh/yLow/yHigh are raw samples at those inset positions, not
        // at 0/width or 0/height. Extrapolate them out to the true edges so the saved range (which
        // lvgl_pointer.h maps onto the full [0, resolution) display range) lines up correctly
        // across the whole screen instead of being off by a margin's worth of scale and offset.
        const auto width = lv_obj_get_content_width(root);
        const auto height = lv_obj_get_content_height(root);
        const int32_t xSpan = static_cast<int32_t>(width) - 2 * TARGET_MARGIN;
        const int32_t ySpan = static_cast<int32_t>(height) - 2 * TARGET_MARGIN;

        if (xSpan <= 0 || ySpan <= 0) {
            lv_label_set_text(titleLabel, "Calibration Failed");
            lv_label_set_text(hintLabel, "Screen too small. Tap to close.");
            lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
            setResult(Result::Error);
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
            lv_label_set_text(titleLabel, "Calibration Failed");
            lv_label_set_text(hintLabel, "Range invalid. Tap to close.");
            lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
            setResult(Result::Error);
            return;
        }

        if (!settings::touch::save(settings)) {
            lv_label_set_text(titleLabel, "Calibration Failed");
            lv_label_set_text(hintLabel, "Unable to save settings. Tap to close.");
            lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
            setResult(Result::Error);
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
        calibrationApplied = true;

        LOG_I(TAG, "Saved calibration x=[%d, %d] y=[%d, %d]", xMin, xMax, yMin, yMax);
        lv_label_set_text(titleLabel, "Calibration Complete");
        lv_label_set_text(hintLabel, "Touch anywhere to continue.");
        lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
        setResult(Result::Ok);
    }

    void onPressInternal(lv_event_t* event) {
        auto* indev = lv_event_get_indev(event);
        if (indev == nullptr) {
            return;
        }

        lv_point_t point = {0, 0};
        lv_indev_get_point(indev, &point);

        if (sampleCount < 4) {
            samples[sampleCount] = {
                .x = static_cast<uint16_t>(std::max(static_cast<lv_coord_t>(0), point.x)),
                .y = static_cast<uint16_t>(std::max(static_cast<lv_coord_t>(0), point.y)),
            };
            sampleCount++;

            if (sampleCount < 4) {
                updateUi();
            } else {
                finishCalibration();
            }
            return;
        }

        stop(manifest.appId);
    }

public:

    void onCreate(AppContext& app) override {
        (void)app;
        // Clear any active calibration so the taps sampled below are raw, uncalibrated coordinates.
        lvgl_lock();
        auto* indev = lvgl_pointer_get_default();
        if (indev != nullptr) {
            lvgl_pointer_set_calibration(indev, nullptr);
        }
        lvgl_unlock();
    }

    void onDestroy(AppContext& app) override {
        (void)app;
        // finishCalibration() already applied a new calibration on success. On cancel/failure,
        // restore whatever calibration was on disk before onCreate() cleared it above.
        if (calibrationApplied) {
            return;
        }

        settings::touch::TouchCalibrationSettings settings;
        lvgl_lock();
        auto* indev = lvgl_pointer_get_default();
        if (indev != nullptr && settings::touch::load(settings) && settings.enabled && settings::touch::isValid(settings)) {
            LvglPointerCalibration calibration = {
                .x_min = settings.xMin,
                .x_max = settings.xMax,
                .y_min = settings.yMin,
                .y_max = settings.yMax,
            };
            lvgl_pointer_set_calibration(indev, &calibration);
        }
        lvgl_unlock();
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        (void)app;

        lv_obj_set_style_bg_color(parent, lv_color_black(), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(parent, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_radius(parent, 0, LV_STATE_DEFAULT);

        root = lv_obj_create(parent);
        lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(root, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(root, 0, LV_STATE_DEFAULT);

        titleLabel = lv_label_create(root);
        lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 14);
        lv_obj_set_style_text_color(titleLabel, lv_color_white(), LV_STATE_DEFAULT);
        lv_label_set_text(titleLabel, "Touchscreen Calibration");

        hintLabel = lv_label_create(root);
        lv_obj_align(hintLabel, LV_ALIGN_BOTTOM_MID, 0, -14);
        lv_obj_set_style_text_color(hintLabel, lv_color_white(), LV_STATE_DEFAULT);
        lv_label_set_text(hintLabel, "Tap target 1/4");

        target = lv_button_create(root);
        lv_obj_set_size(target, 28, 28);
        lv_obj_set_style_radius(target, LV_RADIUS_CIRCLE, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(target, lv_palette_main(LV_PALETTE_RED), LV_STATE_DEFAULT);
        // Ensure root receives all presses for sampling.
        lv_obj_remove_flag(target, LV_OBJ_FLAG_CLICKABLE);

        auto* targetLabel = lv_label_create(target);
        lv_label_set_text(targetLabel, "+");
        lv_obj_center(targetLabel);

        lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(root, onPress, LV_EVENT_PRESSED, this);

        updateUi();
    }
};

extern const AppManifest manifest = {
    .appId = "TouchCalibration",
    .appName = "Touch Calibration",
    .appCategory = Category::Settings,
    .appFlags = AppManifest::Flags::HideStatusBar,
    .createApp = create<TouchCalibrationApp>
};

} // namespace tt::app::touchcalibration

#endif // defined(CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED)
