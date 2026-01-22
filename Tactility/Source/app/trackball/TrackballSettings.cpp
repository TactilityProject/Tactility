//#ifdef ESP_PLATFORM

#include <Tactility/Tactility.h>

#include <Tactility/settings/TrackballSettings.h>
#include <Tactility/Assets.h>
#include <Tactility/lvgl/Toolbar.h>

#include <lvgl.h>

// Forward declare driver functions
namespace trackball {
    void setEnabled(bool enabled);
    enum class Mode { Encoder, Pointer };
    void setMode(Mode mode);
    void setEncoderSensitivity(uint8_t sensitivity);
    void setPointerSensitivity(uint8_t sensitivity);
}

namespace tt::app::trackballsettings {

constexpr auto* TAG = "TrackballSettings";

static trackball::Mode toDriverMode(settings::trackball::TrackballMode mode) {
    switch (mode) {
        case settings::trackball::TrackballMode::Encoder: return trackball::Mode::Encoder;
        case settings::trackball::TrackballMode::Pointer: return trackball::Mode::Pointer;
    }
    return trackball::Mode::Encoder; // default
}

class TrackballSettingsApp final : public App {

    settings::trackball::TrackballSettings tbSettings;
    bool updated = false;
    lv_obj_t* switchTrackball = nullptr;
    lv_obj_t* trackballModeDropdown = nullptr;
    lv_obj_t* encoderSensitivitySlider = nullptr;
    lv_obj_t* pointerSensitivitySlider = nullptr;

    static void onTrackballSwitch(lv_event_t* e) {
        auto* app = static_cast<TrackballSettingsApp*>(lv_event_get_user_data(e));
        bool enabled = lv_obj_has_state(app->switchTrackball, LV_STATE_CHECKED);
        app->tbSettings.trackballEnabled = enabled;
        app->updated = true;
        trackball::setEnabled(enabled);

        // Enable/disable controls based on trackball state
        if (enabled) {
            if (app->trackballModeDropdown) lv_obj_clear_state(app->trackballModeDropdown, LV_STATE_DISABLED);
            if (app->encoderSensitivitySlider) lv_obj_clear_state(app->encoderSensitivitySlider, LV_STATE_DISABLED);
            if (app->pointerSensitivitySlider) lv_obj_clear_state(app->pointerSensitivitySlider, LV_STATE_DISABLED);
        } else {
            if (app->trackballModeDropdown) lv_obj_add_state(app->trackballModeDropdown, LV_STATE_DISABLED);
            if (app->encoderSensitivitySlider) lv_obj_add_state(app->encoderSensitivitySlider, LV_STATE_DISABLED);
            if (app->pointerSensitivitySlider) lv_obj_add_state(app->pointerSensitivitySlider, LV_STATE_DISABLED);
        }
    }

    static void onTrackballModeChanged(lv_event_t* e) {
        auto* app = static_cast<TrackballSettingsApp*>(lv_event_get_user_data(e));
        uint32_t selected = lv_dropdown_get_selected(app->trackballModeDropdown);

        // Validate selection matches expected enum values (dropdown order: Encoder=0, Pointer=1)
        settings::trackball::TrackballMode mode;
        switch (selected) {
            case 0: mode = settings::trackball::TrackballMode::Encoder; break;
            case 1: mode = settings::trackball::TrackballMode::Pointer; break;
            default: return; // Invalid selection, ignore
        }

        app->tbSettings.trackballMode = mode;
        app->updated = true;

        // Apply mode change immediately
        trackball::setMode(toDriverMode(mode));
    }

    static void onEncoderSensitivityChanged(lv_event_t* e) {
        auto* app = static_cast<TrackballSettingsApp*>(lv_event_get_user_data(e));
        int32_t value = lv_slider_get_value(app->encoderSensitivitySlider);
        app->tbSettings.encoderSensitivity = static_cast<uint8_t>(value);
        app->updated = true;

        // Apply immediately
        trackball::setEncoderSensitivity(static_cast<uint8_t>(value));
    }

    static void onPointerSensitivityChanged(lv_event_t* e) {
        auto* app = static_cast<TrackballSettingsApp*>(lv_event_get_user_data(e));
        int32_t value = lv_slider_get_value(app->pointerSensitivitySlider);
        app->tbSettings.pointerSensitivity = static_cast<uint8_t>(value);
        app->updated = true;

        // Apply immediately
        trackball::setPointerSensitivity(static_cast<uint8_t>(value));
    }

public:
    void onShow(AppContext& app, lv_obj_t* parent) override {
        tbSettings = settings::trackball::loadOrGetDefault();
        auto ui_scale = hal::getConfiguration()->uiScale;
        updated = false;

        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        lv_obj_t* toolbar = lvgl::toolbar_create(parent, app);

        switchTrackball = lvgl::toolbar_add_switch_action(toolbar);
        lv_obj_add_event_cb(switchTrackball, onTrackballSwitch, LV_EVENT_VALUE_CHANGED, this);
        if (tbSettings.trackballEnabled) lv_obj_add_state(switchTrackball, LV_STATE_CHECKED);

        auto* main_wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(main_wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(main_wrapper, 1);

        // Trackball mode dropdown
        auto* tb_mode_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(tb_mode_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(tb_mode_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(tb_mode_wrapper, 0, LV_STATE_DEFAULT);

        auto* tb_mode_label = lv_label_create(tb_mode_wrapper);
        lv_label_set_text(tb_mode_label, "Mode");
        lv_obj_align(tb_mode_label, LV_ALIGN_LEFT_MID, 0, 0);

        trackballModeDropdown = lv_dropdown_create(tb_mode_wrapper);
        lv_dropdown_set_options(trackballModeDropdown, "Encoder\nPointer");
        lv_obj_align(trackballModeDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_border_color(trackballModeDropdown, lv_color_hex(0xFAFAFA), LV_PART_MAIN);
        lv_obj_set_style_border_width(trackballModeDropdown, 1, LV_PART_MAIN);
        lv_dropdown_set_selected(trackballModeDropdown, static_cast<uint32_t>(tbSettings.trackballMode));
        lv_obj_add_event_cb(trackballModeDropdown, onTrackballModeChanged, LV_EVENT_VALUE_CHANGED, this);

        // Disable dropdown if trackball is disabled
        if (!tbSettings.trackballEnabled) {
            lv_obj_add_state(trackballModeDropdown, LV_STATE_DISABLED);
        }

        // Encoder sensitivity slider
        auto* enc_sens_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(enc_sens_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(enc_sens_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(enc_sens_wrapper, 0, LV_STATE_DEFAULT);
        if (ui_scale != hal::UiScale::Smallest) {
            lv_obj_set_style_pad_ver(enc_sens_wrapper, 4, LV_STATE_DEFAULT);
        }

        auto* enc_sens_label = lv_label_create(enc_sens_wrapper);
        lv_label_set_text(enc_sens_label, "Encoder Speed");
        lv_obj_align(enc_sens_label, LV_ALIGN_LEFT_MID, 0, 0);

        encoderSensitivitySlider = lv_slider_create(enc_sens_wrapper);
        lv_slider_set_range(encoderSensitivitySlider, 1, 10);
        lv_slider_set_value(encoderSensitivitySlider, tbSettings.encoderSensitivity, LV_ANIM_OFF);
        lv_obj_set_width(encoderSensitivitySlider, LV_PCT(50));
        lv_obj_align(encoderSensitivitySlider, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(encoderSensitivitySlider, onEncoderSensitivityChanged, LV_EVENT_VALUE_CHANGED, this);

        if (!tbSettings.trackballEnabled) {
            lv_obj_add_state(encoderSensitivitySlider, LV_STATE_DISABLED);
        }

        // Pointer sensitivity slider
        auto* ptr_sens_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(ptr_sens_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(ptr_sens_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ptr_sens_wrapper, 0, LV_STATE_DEFAULT);
        if (ui_scale != hal::UiScale::Smallest) {
            lv_obj_set_style_pad_ver(ptr_sens_wrapper, 4, LV_STATE_DEFAULT);
        }

        auto* ptr_sens_label = lv_label_create(ptr_sens_wrapper);
        lv_label_set_text(ptr_sens_label, "Pointer Speed");
        lv_obj_align(ptr_sens_label, LV_ALIGN_LEFT_MID, 0, 0);

        pointerSensitivitySlider = lv_slider_create(ptr_sens_wrapper);
        lv_slider_set_range(pointerSensitivitySlider, 1, 10);
        lv_slider_set_value(pointerSensitivitySlider, tbSettings.pointerSensitivity, LV_ANIM_OFF);
        lv_obj_set_width(pointerSensitivitySlider, LV_PCT(50));
        lv_obj_align(pointerSensitivitySlider, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(pointerSensitivitySlider, onPointerSensitivityChanged, LV_EVENT_VALUE_CHANGED, this);

        if (!tbSettings.trackballEnabled) {
            lv_obj_add_state(pointerSensitivitySlider, LV_STATE_DISABLED);
        }
    }

    void onHide(TT_UNUSED AppContext& app) override {
        if (updated) {
            const auto copy = tbSettings;
            getMainDispatcher().dispatch([copy]{ settings::trackball::save(copy); });
            updated = false;
        }
    }
};

extern const AppManifest manifest = {
    .appId = "TrackballSettings",
    .appName = "Trackball",
    .appIcon = TT_ASSETS_APP_ICON_SETTINGS,
    .appCategory = Category::Settings,
    .createApp = create<TrackballSettingsApp>
};

}

//#endif
