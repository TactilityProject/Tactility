#include <lvgl/lvgl.h>
#include <tactility/device.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/display.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <Tactility/Tactility.h>
#ifdef ESP_PLATFORM
#include <Tactility/service/displayidle/DisplayIdleService.h>
#endif
#include <Tactility/settings/DisplaySettings.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/widgets/toolbar.h>

#include <lvgl.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

namespace tt::app::kerneldisplay {

extern const ::AppManifest manifest;

constexpr auto* TAG = "KernelDisplay";

namespace {

struct Context {
    uint32_t appInstanceId;
    settings::display::DisplaySettings displaySettings;
    bool displaySettingsUpdated = false;
    lv_obj_t* timeoutSwitch = nullptr;
    lv_obj_t* timeoutDropdown = nullptr;
    lv_obj_t* screensaverDropdown = nullptr;
};


Device* getBacklightDevice() {
    Device* display;
    check(device_get_first_by_type(&DISPLAY_TYPE, &display) == ERROR_NONE);
    // Boards not yet migrated to the kernel display driver register a placeholder device (so the
    // devicetree node resolves) with a NULL api - nothing for display_get_backlight() to act on.
    if (device_get_driver(display)->api == nullptr) {
        device_put(display);
        return nullptr;
    }
    Device* backlight = nullptr;
    display_get_backlight(display, &backlight);
    device_put(display);
    return backlight;
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

void onBacklightSliderEvent(lv_event_t* event) {
    auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* backlight = getBacklightDevice();
    assert(backlight != nullptr);

    int32_t slider_value = lv_slider_get_value(slider);
    ctx->displaySettings.backlightDuty = static_cast<uint8_t>(slider_value);
    ctx->displaySettingsUpdated = true;
    backlight_set_brightness(backlight, ctx->displaySettings.backlightDuty);
}

void onOrientationSet(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
    uint32_t selected_index = lv_dropdown_get_selected(dropdown);
    LOG_I(TAG, "Selected %u", (unsigned)selected_index);
    auto selected_orientation = static_cast<settings::display::Orientation>(selected_index);
    if (selected_orientation != ctx->displaySettings.orientation) {
        ctx->displaySettings.orientation = selected_orientation;
        ctx->displaySettingsUpdated = true;
        lv_display_set_rotation(lv_display_get_default(), settings::display::toLvglDisplayRotation(selected_orientation));
    }
}

void onTimeoutSwitch(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ctx->displaySettings.backlightTimeoutEnabled = enabled;
    ctx->displaySettingsUpdated = true;
    if (ctx->timeoutDropdown) {
        if (enabled) {
            lv_obj_clear_state(ctx->timeoutDropdown, LV_STATE_DISABLED);
            if (ctx->screensaverDropdown) {
                lv_obj_clear_state(ctx->screensaverDropdown, LV_STATE_DISABLED);
            }
        } else {
            lv_obj_add_state(ctx->timeoutDropdown, LV_STATE_DISABLED);
            if (ctx->screensaverDropdown) {
                lv_obj_add_state(ctx->screensaverDropdown, LV_STATE_DISABLED);
            }
        }
    }
}

void onTimeoutChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
    uint32_t idx = lv_dropdown_get_selected(dropdown);
    // Map dropdown index to ms: 0=15s,1=30s,2=1m,3=2m,4=5m,5=Never
    static const uint32_t values_ms[] = {15000, 30000, 60000, 120000, 300000, 0};
    if (idx < (sizeof(values_ms)/sizeof(values_ms[0]))) {
        ctx->displaySettings.backlightTimeoutMs = values_ms[idx];
        ctx->displaySettingsUpdated = true;
    }
}

void onScreensaverChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
    uint32_t idx = lv_dropdown_get_selected(dropdown);
    // Validate index bounds before casting to enum
    if (idx >= static_cast<uint32_t>(settings::display::ScreensaverType::Count)) {
        return;
    }
    auto selected_type = static_cast<settings::display::ScreensaverType>(idx);
    if (selected_type != ctx->displaySettings.screensaverType) {
        ctx->displaySettings.screensaverType = selected_type;
        ctx->displaySettingsUpdated = true;
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    ctx->displaySettings = settings::display::loadOrGetDefault();
    auto ui_density = lvgl_get_ui_density();

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* backlight = getBacklightDevice();

    auto* toolbar = lvgl_toolbar_create(parent, "Display");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    // Backlight slider
    // Note: no gamma slider here - unlike HalDisplayApp (app/display/Display.cpp), the kernel
    // DisplayApi has no gamma curve control yet.

    if (backlight != nullptr) {
        bool is_on_off_brightness = backlight_get_min_brightness(backlight) == 0 && backlight_get_max_brightness(backlight) == 1;
        if (!is_on_off_brightness) {
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
            lv_obj_add_event_cb(brightness_slider, onBacklightSliderEvent, LV_EVENT_VALUE_CHANGED, ctx);

            lv_slider_set_value(brightness_slider, ctx->displaySettings.backlightDuty, LV_ANIM_OFF);
        }
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
    lv_obj_add_event_cb(orientation_dropdown, onOrientationSet, LV_EVENT_VALUE_CHANGED, ctx);
    // Set the dropdown to match current orientation enum
    lv_dropdown_set_selected(orientation_dropdown, static_cast<uint16_t>(ctx->displaySettings.orientation));

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

        ctx->timeoutSwitch = lv_switch_create(timeout_wrapper);
        if (ctx->displaySettings.backlightTimeoutEnabled) {
            lv_obj_add_state(ctx->timeoutSwitch, LV_STATE_CHECKED);
        }
        lv_obj_align(ctx->timeoutSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(ctx->timeoutSwitch, onTimeoutSwitch, LV_EVENT_VALUE_CHANGED, ctx);

        auto* timeout_select_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(timeout_select_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(timeout_select_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(timeout_select_wrapper, 0, LV_STATE_DEFAULT);

        auto* timeout_value_label = lv_label_create(timeout_select_wrapper);
        lv_label_set_text(timeout_value_label, "Timeout");
        lv_obj_align(timeout_value_label, LV_ALIGN_LEFT_MID, 0, 0);

        ctx->timeoutDropdown = lv_dropdown_create(timeout_select_wrapper);
        lv_dropdown_set_options(ctx->timeoutDropdown, "15 seconds\n30 seconds\n1 minute\n2 minutes\n5 minutes\nNever");
        lv_obj_align(ctx->timeoutDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(ctx->timeoutDropdown, onTimeoutChanged, LV_EVENT_VALUE_CHANGED, ctx);
        // Initialize dropdown selection from settings
        uint32_t ms = ctx->displaySettings.backlightTimeoutMs;
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
        lv_dropdown_set_selected(ctx->timeoutDropdown, idx);
        if (!ctx->displaySettings.backlightTimeoutEnabled) {
            lv_obj_add_state(ctx->timeoutDropdown, LV_STATE_DISABLED);
        }

        // Screensaver type
        auto* screensaver_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(screensaver_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(screensaver_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(screensaver_wrapper, 0, LV_STATE_DEFAULT);

        auto* screensaver_label = lv_label_create(screensaver_wrapper);
        lv_label_set_text(screensaver_label, "Screensaver");
        lv_obj_align(screensaver_label, LV_ALIGN_LEFT_MID, 0, 0);

        ctx->screensaverDropdown = lv_dropdown_create(screensaver_wrapper);
        // Note: order correlates with settings::display::ScreensaverType enum order
        lv_dropdown_set_options(ctx->screensaverDropdown, "None\nBouncing Balls\nMystify\nMatrix Rain\nStackChan");
        lv_obj_align(ctx->screensaverDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(ctx->screensaverDropdown, onScreensaverChanged, LV_EVENT_VALUE_CHANGED, ctx);
        lv_dropdown_set_selected(ctx->screensaverDropdown, static_cast<uint16_t>(ctx->displaySettings.screensaverType));
        if (!ctx->displaySettings.backlightTimeoutEnabled) {
            lv_obj_add_state(ctx->screensaverDropdown, LV_STATE_DISABLED);
        }
    }
}

// Mirrors the old onHide() behaviour: persist the settings (regardless of whether the app is
// giving up its thread for a save/resume cycle, or closing for good) whenever they changed.
void persistIfUpdated(Context& ctx) {
    if (ctx.displaySettingsUpdated) {
        // Dispatch it, so file IO doesn't block the UI
        const settings::display::DisplaySettings settings_to_save = ctx.displaySettings;
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

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

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
                persistIfUpdated(ctx);
                app_manager_finish(appInstanceId);
                shouldClose = true;
                break;
            default:
                break;
        }
    }

    window_manager_remove(window);
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "Display",
    .name = "Display",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

} // namespace tt::app::kerneldisplay
