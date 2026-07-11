#include <tactility/drivers/display.h>
#include <tactility/error.h>
#include <tactility/lvgl_icon_shared.h>
#include <tactility/device.h>
#include <tactility/drivers/backlight.h>
#include <tactility/log.h>
#include <tactility/lvgl_module.h>

#include <Tactility/Tactility.h>
#ifdef ESP_PLATFORM
#include <Tactility/service/displayidle/DisplayIdleService.h>
#endif
#include <Tactility/app/App.h>
#include <Tactility/lvgl/Toolbar.h>
#include <Tactility/settings/DisplaySettings.h>

#include <lvgl.h>

namespace tt::app::kerneldisplay {

constexpr auto* TAG = "KernelDisplay";

static Device* getBacklightDevice() {
    Device* display = device_find_first_by_type(&DISPLAY_TYPE);
    check(display);
    // Boards not yet migrated to the kernel display driver register a placeholder device (so the
    // devicetree node resolves) with a NULL api - nothing for display_get_backlight() to act on.
    if (device_get_driver(display)->api == nullptr) {
        return nullptr;
    }
    Device* backlight = nullptr;
    return display_get_backlight(display, &backlight) == ERROR_NONE ? backlight : nullptr;
}

class KernelDisplayApp final : public App {

    settings::display::DisplaySettings displaySettings;
    bool displaySettingsUpdated = false;
    lv_obj_t* timeoutSwitch = nullptr;
    lv_obj_t* timeoutDropdown = nullptr;
    lv_obj_t* screensaverDropdown = nullptr;

    static void onBacklightSliderEvent(lv_event_t* event) {
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(event));
        auto* app = static_cast<KernelDisplayApp*>(lv_event_get_user_data(event));
        auto* backlight = getBacklightDevice();
        assert(backlight != nullptr);

        int32_t slider_value = lv_slider_get_value(slider);
        app->displaySettings.backlightDuty = static_cast<uint8_t>(slider_value);
        app->displaySettingsUpdated = true;
        backlight_set_brightness(backlight, app->displaySettings.backlightDuty);
    }

    static void onOrientationSet(lv_event_t* event) {
        auto* app = static_cast<KernelDisplayApp*>(lv_event_get_user_data(event));
        auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
        uint32_t selected_index = lv_dropdown_get_selected(dropdown);
        LOG_I(TAG, "Selected %u", (unsigned)selected_index);
        auto selected_orientation = static_cast<settings::display::Orientation>(selected_index);
        if (selected_orientation != app->displaySettings.orientation) {
            app->displaySettings.orientation = selected_orientation;
            app->displaySettingsUpdated = true;
            lv_display_set_rotation(lv_display_get_default(), settings::display::toLvglDisplayRotation(selected_orientation));
        }
    }

    static void onTimeoutSwitch(lv_event_t* event) {
        auto* app = static_cast<KernelDisplayApp*>(lv_event_get_user_data(event));
        auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(event));
        bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        app->displaySettings.backlightTimeoutEnabled = enabled;
        app->displaySettingsUpdated = true;
        if (app->timeoutDropdown) {
            if (enabled) {
                lv_obj_clear_state(app->timeoutDropdown, LV_STATE_DISABLED);
                if (app->screensaverDropdown) {
                    lv_obj_clear_state(app->screensaverDropdown, LV_STATE_DISABLED);
                }
            } else {
                lv_obj_add_state(app->timeoutDropdown, LV_STATE_DISABLED);
                if (app->screensaverDropdown) {
                    lv_obj_add_state(app->screensaverDropdown, LV_STATE_DISABLED);
                }
            }
        }
    }

    static void onTimeoutChanged(lv_event_t* event) {
        auto* app = static_cast<KernelDisplayApp*>(lv_event_get_user_data(event));
        auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
        uint32_t idx = lv_dropdown_get_selected(dropdown);
        // Map dropdown index to ms: 0=15s,1=30s,2=1m,3=2m,4=5m,5=Never
        static const uint32_t values_ms[] = {15000, 30000, 60000, 120000, 300000, 0};
        if (idx < (sizeof(values_ms)/sizeof(values_ms[0]))) {
            app->displaySettings.backlightTimeoutMs = values_ms[idx];
            app->displaySettingsUpdated = true;
        }
    }

    static void onScreensaverChanged(lv_event_t* event) {
        auto* app = static_cast<KernelDisplayApp*>(lv_event_get_user_data(event));
        auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
        uint32_t idx = lv_dropdown_get_selected(dropdown);
        // Validate index bounds before casting to enum
        if (idx >= static_cast<uint32_t>(settings::display::ScreensaverType::Count)) {
            return;
        }
        auto selected_type = static_cast<settings::display::ScreensaverType>(idx);
        if (selected_type != app->displaySettings.screensaverType) {
            app->displaySettings.screensaverType = selected_type;
            app->displaySettingsUpdated = true;
        }
    }

public:

    void onShow(AppContext& app, lv_obj_t* parent) override {
        displaySettings = settings::display::loadOrGetDefault();
        auto ui_density = lvgl_get_ui_density();

        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

        auto* backlight = getBacklightDevice();

        lvgl::toolbar_create(parent, app);

        auto* main_wrapper = lv_obj_create(parent);
        lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_width(main_wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(main_wrapper, 1);

        // Backlight slider
        // Note: no gamma slider here - unlike HalDisplayApp (app/display/Display.cpp), the kernel
        // DisplayApi has no gamma curve control yet.

        if (backlight != nullptr) {
            auto* brightness_wrapper = lv_obj_create(main_wrapper);
            lv_obj_set_size(brightness_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_hor(brightness_wrapper, 0, LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(brightness_wrapper, 0, LV_STATE_DEFAULT);
            if (ui_density != LVGL_UI_DENSITY_COMPACT) {
                lv_obj_set_style_pad_ver(brightness_wrapper, 4, LV_STATE_DEFAULT);
            }

            auto* brightness_label = lv_label_create(brightness_wrapper);
            lv_label_set_text(brightness_label, "Brightness");
            lv_obj_align(brightness_label, LV_ALIGN_LEFT_MID, 0, 0);

            auto* brightness_slider = lv_slider_create(brightness_wrapper);
            lv_obj_set_width(brightness_slider, LV_PCT(50));
            lv_obj_align(brightness_slider, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_slider_set_range(brightness_slider, backlight_get_min_brightness(backlight), backlight_get_max_brightness(backlight));
            lv_obj_add_event_cb(brightness_slider, onBacklightSliderEvent, LV_EVENT_VALUE_CHANGED, this);

            lv_slider_set_value(brightness_slider, displaySettings.backlightDuty, LV_ANIM_OFF);
        }

        // Orientation

        auto* orientation_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(orientation_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(orientation_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(orientation_wrapper, 0, LV_STATE_DEFAULT);

        auto* orientation_label = lv_label_create(orientation_wrapper);
        lv_label_set_text(orientation_label, "Orientation");
        lv_obj_align(orientation_label, LV_ALIGN_LEFT_MID, 0, 0);

        auto* orientation_dropdown = lv_dropdown_create(orientation_wrapper);
        // Note: order correlates with settings::display::Orientation item order
        lv_dropdown_set_options(orientation_dropdown, "Landscape\nPortrait Right\nLandscape Flipped\nPortrait Left");
        lv_obj_align(orientation_dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(orientation_dropdown, onOrientationSet, LV_EVENT_VALUE_CHANGED, this);
        // Set the dropdown to match current orientation enum
        lv_dropdown_set_selected(orientation_dropdown, static_cast<uint16_t>(displaySettings.orientation));

        // Screen timeout
        // Note: DisplayIdleService doesn't act on these settings for kernel-driver displays yet
        // (it only looks up the deprecated tt::hal::display::DisplayDevice), so these currently
        // just get saved without taking effect. Kept for parity/forward-compatibility.

        if (backlight != nullptr) {
            auto* timeout_wrapper = lv_obj_create(main_wrapper);
            lv_obj_set_size(timeout_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(timeout_wrapper, 0, LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(timeout_wrapper, 0, LV_STATE_DEFAULT);

            auto* timeout_label = lv_label_create(timeout_wrapper);
            lv_label_set_text(timeout_label, "Auto screen off");
            lv_obj_align(timeout_label, LV_ALIGN_LEFT_MID, 0, 0);

            timeoutSwitch = lv_switch_create(timeout_wrapper);
            if (displaySettings.backlightTimeoutEnabled) {
                lv_obj_add_state(timeoutSwitch, LV_STATE_CHECKED);
            }
            lv_obj_align(timeoutSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_add_event_cb(timeoutSwitch, onTimeoutSwitch, LV_EVENT_VALUE_CHANGED, this);

            auto* timeout_select_wrapper = lv_obj_create(main_wrapper);
            lv_obj_set_size(timeout_select_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(timeout_select_wrapper, 0, LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(timeout_select_wrapper, 0, LV_STATE_DEFAULT);

            auto* timeout_value_label = lv_label_create(timeout_select_wrapper);
            lv_label_set_text(timeout_value_label, "Timeout");
            lv_obj_align(timeout_value_label, LV_ALIGN_LEFT_MID, 0, 0);

            timeoutDropdown = lv_dropdown_create(timeout_select_wrapper);
            lv_dropdown_set_options(timeoutDropdown, "15 seconds\n30 seconds\n1 minute\n2 minutes\n5 minutes\nNever");
            lv_obj_align(timeoutDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_add_event_cb(timeoutDropdown, onTimeoutChanged, LV_EVENT_VALUE_CHANGED, this);
            // Initialize dropdown selection from settings
            uint32_t ms = displaySettings.backlightTimeoutMs;
            uint32_t idx = 2; // default 1 minute
            if (ms == 15000) idx = 0;
            else if (ms == 30000)
                idx = 1;
            else if (ms == 60000)
                idx = 2;
            else if (ms == 120000)
                idx = 3;
            else if (ms == 300000)
                idx = 4;
            else if (ms == 0)
                idx = 5;
            lv_dropdown_set_selected(timeoutDropdown, idx);
            if (!displaySettings.backlightTimeoutEnabled) {
                lv_obj_add_state(timeoutDropdown, LV_STATE_DISABLED);
            }

            // Screensaver type
            auto* screensaver_wrapper = lv_obj_create(main_wrapper);
            lv_obj_set_size(screensaver_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_pad_all(screensaver_wrapper, 0, LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(screensaver_wrapper, 0, LV_STATE_DEFAULT);

            auto* screensaver_label = lv_label_create(screensaver_wrapper);
            lv_label_set_text(screensaver_label, "Screensaver");
            lv_obj_align(screensaver_label, LV_ALIGN_LEFT_MID, 0, 0);

            screensaverDropdown = lv_dropdown_create(screensaver_wrapper);
            // Note: order correlates with settings::display::ScreensaverType enum order
            lv_dropdown_set_options(screensaverDropdown, "None\nBouncing Balls\nMystify\nMatrix Rain\nStackChan");
            lv_obj_align(screensaverDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_add_event_cb(screensaverDropdown, onScreensaverChanged, LV_EVENT_VALUE_CHANGED, this);
            lv_dropdown_set_selected(screensaverDropdown, static_cast<uint16_t>(displaySettings.screensaverType));
            if (!displaySettings.backlightTimeoutEnabled) {
                lv_obj_add_state(screensaverDropdown, LV_STATE_DISABLED);
            }
        }

        // Note: no touch calibration section here - unlike HalDisplayApp, the kernel PointerApi has
        // no calibration support yet.
    }

    void onHide(AppContext& app) override {
        if (displaySettingsUpdated) {
            // Dispatch it, so file IO doesn't block the UI
            const settings::display::DisplaySettings settings_to_save = displaySettings;
            getMainDispatcher().dispatch([settings_to_save] {
                settings::display::save(settings_to_save);
#ifdef ESP_PLATFORM
                // Notify DisplayIdle service to reload settings
                auto displayIdle = service::displayidle::findService();
                if (displayIdle) {
                    displayIdle->reloadSettings();
                }
#endif
            });
        }
    }
};

extern const AppManifest manifest = {
    .appId = "Display",
    .appName = "Display",
    .appIcon = LVGL_ICON_SHARED_DISPLAY_SETTINGS,
    .appCategory = Category::Settings,
    .createApp = create<KernelDisplayApp>
};

} // namespace
