#define L10N_APP "display"
#include <Tactility/i18n/AppDomain.h>
#include <lvgl/lvgl.h>
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/imu.h>
#include <tactility/error.h>
#include <tactility/file_ops.h>
#include <tactility/log.h>

#include <Tactility/Tactility.h>
#include <Tactility/service/autorotate/AutoRotateService.h>
#include <Tactility/settings/DisplaySettings.h>

#include <app/event.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <algorithm>
#include <new>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/sliderbox.h>
#include <lvgl/widgets/toolbar.h>

#include <lvgl.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

namespace tt::app::display {

extern const ::AppManifest manifest;

constexpr auto* TAG = "Display";

namespace {

// The icon is an upright phone with its bar at the bottom, rotated clockwise (0.1 degree units) to the screen's shape.
// In settings::display::Orientation order: Landscape, Portrait, LandscapeFlipped, PortraitFlipped
constexpr int32_t ORIENTATION_ICON_ROTATIONS[] = { 2700, 0, 900, 1800 };

struct Context {
    uint32_t appInstanceId;
    settings::display::DisplaySettings displaySettings;
    bool displaySettingsUpdated = false;
    TaskEventGroup* eventGroup = nullptr;
    // Widgets, valid while the window is shown
    // Shown when auto-rotate is off or unavailable: the orientation label and chips
    lv_obj_t* orientationCard = nullptr;
    lv_obj_t* orientationChips = nullptr;
    lv_obj_t* autoRotateSwitch = nullptr;
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
    app_event_emit_close(ctx->appInstanceId);
}

constexpr int32_t BRIGHTNESS_STEP = 10;

// The slider shows a percentage of the backlight's brightness range
int32_t percentToBrightness(Device* backlight, int32_t percent) {
    const int32_t min = backlight_get_min_brightness(backlight);
    const int32_t range = backlight_get_max_brightness(backlight) - min;
    const int32_t brightness = min + (percent * range + 50) / 100;
    // A brightness of 0 turns the backlight off, so 0% is the dimmest setting that is still on
    return brightness < 1 ? 1 : brightness;
}

int32_t brightnessToPercent(Device* backlight, int32_t brightness) {
    // A backlight with few levels can't map a level back to the exact percentage, so the slider step of the level is used
    for (int32_t percent = 0; percent <= 100; percent += BRIGHTNESS_STEP) {
        if (percentToBrightness(backlight, percent) == brightness) {
            return percent;
        }
    }
    const int32_t min = backlight_get_min_brightness(backlight);
    const int32_t range = backlight_get_max_brightness(backlight) - min;
    // A saved brightness can be out of range, e.g. when it was saved for another backlight driver
    const int32_t clamped = ::std::clamp(brightness, min, min + range);
    return range > 0 ? ((clamped - min) * 100 + range / 2) / range : 100;
}

void onBacklightSliderEvent(lv_event_t* event) {
    auto* slider_box = lv_event_get_current_target_obj(event);
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* backlight = getBacklightDevice();
    assert(backlight != nullptr);

    const int32_t percent = lvgl_sliderbox_get_value(slider_box);
    ctx->displaySettings.backlightDuty = static_cast<uint8_t>(percentToBrightness(backlight, percent));
    ctx->displaySettingsUpdated = true;
    backlight_set_brightness(backlight, ctx->displaySettings.backlightDuty);
    device_put(backlight);
}

void forEachChild(lv_obj_t* parent, auto&& action) {
    const uint32_t count = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < count; i++) {
        action(lv_obj_get_child(parent, static_cast<int32_t>(i)), i);
    }
}

void setCheckedChip(lv_obj_t* chips, settings::display::Orientation orientation) {
    forEachChild(chips, [orientation](lv_obj_t* chip, uint32_t index) {
        lv_obj_set_state(chip, LV_STATE_CHECKED, index == static_cast<uint32_t>(orientation));
    });
}

/** Shows or hides the manual orientation card, depending on whether auto-rotate is on */
void updateOrientationVisibility(Context* ctx) {
    const bool auto_rotate = ctx->autoRotateSwitch != nullptr && ctx->displaySettings.autoRotateEnabled;
    lv_obj_set_hidden(ctx->orientationCard, auto_rotate);
}

void onOrientationChipPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* chip = lv_event_get_target_obj(event);
    const auto selected_index = lv_obj_get_index(chip);
    LOG_I(TAG, "Selected %d", (int)selected_index);
    auto selected_orientation = static_cast<settings::display::Orientation>(selected_index);
    if (selected_orientation != ctx->displaySettings.orientation) {
        ctx->displaySettings.orientation = selected_orientation;
        ctx->displaySettingsUpdated = true;
        lv_display_set_rotation(lv_display_get_default(), settings::display::toLvglDisplayRotation(selected_orientation));
        setCheckedChip(ctx->orientationChips, selected_orientation);
    }
}

void onAutoRotateSwitch(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ctx->displaySettings.autoRotateEnabled = enabled;
    ctx->displaySettingsUpdated = true;
    updateOrientationVisibility(ctx);
}

/** Creates a transparent column in a card with a title, which centers the title and the content below it */
lv_obj_t* createCardGroup(lv_obj_t* card, const char* title) {
    auto* group = lv_obj_create(card);
    lv_obj_set_size(group, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(group, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(group, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(group, false);

    auto* label = lv_label_create(group);
    lv_label_set_text(label, title);
    return group;
}

/**
 * Creates a row of chips with a phone icon per orientation, in settings::display::Orientation order.
 * The row is centered in its parent while the chips fit, and scrolls when they don't.
 */
lv_obj_t* createOrientationChips(lv_obj_t* parent, settings::display::Orientation selected, lv_event_cb_t onPressed, Context* ctx) {
    auto* chips = lv_obj_create(parent);
    lv_obj_set_size(chips, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(chips, LV_PCT(100), LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(chips, LV_DIR_HOR);
    lv_obj_set_style_bg_opa(chips, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(chips, 0, LV_STATE_DEFAULT);
    // The chips' margins leave room for their focus rings and space them apart
    lv_obj_set_style_pad_all(chips, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(chips, 0, LV_STATE_DEFAULT);

    for (int32_t i = 0; i < 4; i++) {
        auto* chip = lvgl_chip_create(chips);
        lv_obj_add_event_cb(chip, onPressed, LV_EVENT_SHORT_CLICKED, ctx);
        auto* icon = lv_label_create(chip);
        lv_obj_set_style_text_font(icon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
        lv_label_set_text(icon, LVGL_ICON_SHARED_PHONE_ANDROID);
        lv_obj_set_style_transform_pivot_x(icon, LV_PCT(50), LV_STATE_DEFAULT);
        lv_obj_set_style_transform_pivot_y(icon, LV_PCT(50), LV_STATE_DEFAULT);
        lv_obj_set_style_transform_rotation(icon, ORIENTATION_ICON_ROTATIONS[i], LV_STATE_DEFAULT);
    }
    lvgl_grid_navigation_add(chips);
    setCheckedChip(chips, selected);
    return chips;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    bool has_imu = device_exists_of_type(&IMU_TYPE);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* backlight = getBacklightDevice();

    auto* toolbar = lvgl_toolbar_create(parent, tr("Display"));
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(main_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    // Backlight slider
    // Note: no gamma slider here - unlike HalDisplayApp (app/display/Display.cpp), the kernel
    // DisplayApi has no gamma curve control yet.

    if (backlight != nullptr) {
        bool is_on_off_brightness = backlight_get_min_brightness(backlight) == 0 && backlight_get_max_brightness(backlight) == 1;
        if (!is_on_off_brightness) {
            auto* brightness_label = lv_label_create(main_wrapper);
            lv_label_set_text(brightness_label, tr("Brightness"));

            auto* brightness_card = lvgl_card_create(main_wrapper);
            lv_obj_set_size(brightness_card, LV_PCT(100), LV_SIZE_CONTENT);

            const int32_t percent = brightnessToPercent(backlight, ctx->displaySettings.backlightDuty);
            auto* brightness_slider = lvgl_sliderbox_create(brightness_card, 0, 100, BRIGHTNESS_STEP, percent);
            lv_obj_set_width(brightness_slider, LV_PCT(100));
            lvgl_sliderbox_add_value_changed_cb(brightness_slider, onBacklightSliderEvent, ctx);
        }
        // Only compared against nullptr below, never dereferenced again, so releasing it here is safe.
        device_put(backlight);
    }

    // Orientation

    // Auto-rotate (IMU-driven), which decides what the card below shows
    if (has_imu) {
        auto* auto_rotate_row = lv_obj_create(main_wrapper);
        lv_obj_set_size(auto_rotate_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(auto_rotate_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(auto_rotate_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_border_width(auto_rotate_row, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(auto_rotate_row, 0, LV_STATE_DEFAULT);
        lv_obj_set_scrollable(auto_rotate_row, false);

        auto* auto_rotate_label = lv_label_create(auto_rotate_row);
        lv_label_set_text(auto_rotate_label, tr("Auto-rotate"));
        lv_obj_set_flex_grow(auto_rotate_label, 1);

        ctx->autoRotateSwitch = lv_switch_create(auto_rotate_row);
        lv_obj_set_state(ctx->autoRotateSwitch, LV_STATE_CHECKED, ctx->displaySettings.autoRotateEnabled);
        lv_obj_add_event_cb(ctx->autoRotateSwitch, onAutoRotateSwitch, LV_EVENT_VALUE_CHANGED, ctx);
    }

    // The card shows the manual orientation; auto-rotate hides it while it drives the rotation
    ctx->orientationCard = lvgl_card_create(main_wrapper);
    lv_obj_set_size(ctx->orientationCard, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->orientationCard, LV_FLEX_FLOW_COLUMN);

    auto* orientation_group = createCardGroup(ctx->orientationCard, tr("Orientation"));
    ctx->orientationChips = createOrientationChips(orientation_group, ctx->displaySettings.orientation, onOrientationChipPressed, ctx);

    updateOrientationVisibility(ctx);
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->orientationCard = nullptr;
    ctx->orientationChips = nullptr;
    ctx->autoRotateSwitch = nullptr;
}

// Mirrors the old onHide() behaviour: persist the settings (regardless of whether the app is
// giving up its thread for a save/resume cycle, or closing for good) whenever they changed.
struct SaveSettingsJob {
    settings::display::DisplaySettings settings;
};

/** File operation: it runs on the file operations task, which frees the job */
void saveSettingsInBackground(void* context) {
    auto* job = static_cast<SaveSettingsJob*>(context);
    settings::display::save(job->settings);
    // Notify AutoRotate service to reload settings
    auto autoRotate = service::autorotate::findService();
    if (autoRotate) {
        autoRotate->reloadSettings();
    }
    delete job;
}

void persistIfUpdated(Context& ctx) {
    if (ctx.displaySettingsUpdated) {
        // Queued, so file IO doesn't block the UI
        auto* job = new (::std::nothrow) SaveSettingsJob { ctx.displaySettings };
        if (job == nullptr || file_ops_queue(saveSettingsInBackground, job, nullptr) != ERROR_NONE) {
            LOG_E(TAG, "Failed to queue saving display settings");
            delete job;
        }
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    // Loaded once: the window is rebuilt from it, e.g. after a dialog closed
    ctx.displaySettings = settings::display::loadOrGetDefault();

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;

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
                    persistIfUpdated(ctx);
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

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.display",
    .name = "Display",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::display
