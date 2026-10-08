#include <lvgl/lvgl.h>
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/imu.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <Tactility/Tactility.h>
#include <Tactility/app/selectiondialog/SelectionDialog.h>
#ifdef ESP_PLATFORM
#include <Tactility/service/displayidle/DisplayIdleService.h>
#endif
#include <Tactility/service/autorotate/AutoRotateService.h>
#include <Tactility/settings/DisplaySettings.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/sliderbox.h>
#include <lvgl/widgets/toolbar.h>

#include <lvgl.h>

#include <string>
#include <vector>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

namespace tt::app::display {

extern const ::AppManifest manifest;

constexpr auto* TAG = "Display";

namespace {

struct TimeoutOption {
    uint32_t ms;
    const char* name;
};

constexpr TimeoutOption TIMEOUT_OPTIONS[] = {
    { 15000, "15 seconds" },
    { 30000, "30 seconds" },
    { 60000, "1 minute" },
    { 120000, "2 minutes" },
    { 300000, "5 minutes" },
    { 0, "Never" },
};

// The icon is an upright phone with its bar at the bottom, rotated clockwise (0.1 degree units) to the screen's shape.
// In settings::display::Orientation order: Landscape, Portrait, LandscapeFlipped, PortraitFlipped
constexpr int32_t ORIENTATION_ICON_ROTATIONS[] = { 2700, 0, 900, 1800 };

// In settings::display::ScreensaverType order
constexpr const char* SCREENSAVER_NAMES[] = { "None", "Bouncing Balls", "Mystify", "Matrix Rain", "StackChan" };

struct Context {
    uint32_t appInstanceId;
    settings::display::DisplaySettings displaySettings;
    bool displaySettingsUpdated = false;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t selectTimeoutBit = 0;
    uint32_t selectScreensaverBit = 0;
    uint32_t timeoutDialogId = 0;
    uint32_t screensaverDialogId = 0;
    // Widgets, valid while the window is shown
    lv_obj_t* timeoutSwitch = nullptr;
    // Shown when the auto screen off is enabled: the timeout and screensaver
    lv_obj_t* timeoutCard = nullptr;
    lv_obj_t* timeoutValueLabel = nullptr;
    lv_obj_t* screensaverValueLabel = nullptr;
    // Shown when auto-rotate is off or unavailable: the orientation label and chips
    lv_obj_t* orientationGroup = nullptr;
    lv_obj_t* orientationChips = nullptr;
    lv_obj_t* autoRotateSwitch = nullptr;
    // Shown when auto-rotate is on: the sensor mounting label and chips
    lv_obj_t* mountRotationGroup = nullptr;
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

// The slider shows a percentage of the backlight's brightness range
int32_t brightnessToPercent(Device* backlight, int32_t brightness) {
    const int32_t min = backlight_get_min_brightness(backlight);
    const int32_t range = backlight_get_max_brightness(backlight) - min;
    return range > 0 ? ((brightness - min) * 100 + range / 2) / range : 100;
}

int32_t percentToBrightness(Device* backlight, int32_t percent) {
    const int32_t min = backlight_get_min_brightness(backlight);
    const int32_t range = backlight_get_max_brightness(backlight) - min;
    const int32_t brightness = min + (percent * range + 50) / 100;
    // A brightness of 0 turns the backlight off, so 0% is the dimmest setting that is still on
    return brightness < 1 ? 1 : brightness;
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

/** Shows the manual orientation or the sensor mounting, depending on whether auto-rotate is on */
void updateOrientationVisibility(Context* ctx) {
    const bool auto_rotate = ctx->autoRotateSwitch != nullptr && ctx->displaySettings.autoRotateEnabled;
    lv_obj_set_flag(ctx->orientationGroup, LV_OBJ_FLAG_HIDDEN, auto_rotate);
    if (ctx->mountRotationGroup != nullptr) {
        lv_obj_set_flag(ctx->mountRotationGroup, LV_OBJ_FLAG_HIDDEN, !auto_rotate);
    }
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

void onMountRotationChipPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* chip = lv_event_get_target_obj(event);
    const auto orientation = static_cast<settings::display::Orientation>(lv_obj_get_index(chip));
    ctx->displaySettings.autoRotateMountRotation = orientation;
    ctx->displaySettingsUpdated = true;
    setCheckedChip(lv_obj_get_parent(chip), orientation);
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
    lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);

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

void onTimeoutSwitch(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(event));
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ctx->displaySettings.backlightTimeoutEnabled = enabled;
    ctx->displaySettingsUpdated = true;
    if (ctx->timeoutCard) {
        lv_obj_set_flag(ctx->timeoutCard, LV_OBJ_FLAG_HIDDEN, !enabled);
    }
}

const char* getTimeoutName(uint32_t ms) {
    for (const auto& option : TIMEOUT_OPTIONS) {
        if (option.ms == ms) {
            return option.name;
        }
    }
    return "Custom";
}

const char* getScreensaverName(settings::display::ScreensaverType type) {
    const auto index = static_cast<size_t>(type);
    return index < std::size(SCREENSAVER_NAMES) ? SCREENSAVER_NAMES[index] : "";
}

/** Requires the LVGL lock */
void updateTimeoutLabels(Context* ctx) {
    if (ctx->timeoutValueLabel != nullptr) {
        lv_label_set_text(ctx->timeoutValueLabel, getTimeoutName(ctx->displaySettings.backlightTimeoutMs));
        lv_label_set_text(ctx->screensaverValueLabel, getScreensaverName(ctx->displaySettings.screensaverType));
    }
}

void onChangeTimeoutPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->selectTimeoutBit);
}

void onChangeScreensaverPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    task_event_group_signal(ctx->eventGroup, ctx->selectScreensaverBit);
}

void startTimeoutSelection(Context* ctx) {
    if (ctx->timeoutDialogId != 0) {
        return;
    }
    std::vector<std::string> items;
    for (const auto& option : TIMEOUT_OPTIONS) {
        items.emplace_back(option.name);
    }
    ctx->timeoutDialogId = selectiondialog::start(ctx->appInstanceId, "Timeout", items);
}

void startScreensaverSelection(Context* ctx) {
    if (ctx->screensaverDialogId != 0) {
        return;
    }
    const std::vector<std::string> items(std::begin(SCREENSAVER_NAMES), std::end(SCREENSAVER_NAMES));
    ctx->screensaverDialogId = selectiondialog::start(ctx->appInstanceId, "Screensaver", items);
}

// Other results than an index mean that the dialog was dismissed
void onTimeoutSelectionResult(Context* ctx, int32_t selected) {
    ctx->timeoutDialogId = 0;
    if (selected >= 0 && selected < static_cast<int32_t>(std::size(TIMEOUT_OPTIONS))) {
        ctx->displaySettings.backlightTimeoutMs = TIMEOUT_OPTIONS[selected].ms;
        ctx->displaySettingsUpdated = true;
    }
}

void onScreensaverSelectionResult(Context* ctx, int32_t selected) {
    ctx->screensaverDialogId = 0;
    if (selected >= 0 && selected < static_cast<int32_t>(settings::display::ScreensaverType::Count)) {
        ctx->displaySettings.screensaverType = static_cast<settings::display::ScreensaverType>(selected);
        ctx->displaySettingsUpdated = true;
    }
}

/** A row in a card: "Title          Value [Change]" */
lv_obj_t* createValueRow(lv_obj_t* card, const char* title, lv_event_cb_t onChange, Context* ctx) {
    auto* row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, 0, LV_STATE_DEFAULT);
    // The card provides the background
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    auto* title_label = lv_label_create(row);
    lv_label_set_text(title_label, title);
    lv_obj_set_flex_grow(title_label, 1);

    auto* value_label = lv_label_create(row);

    auto* button = lv_button_create(row);
    lv_label_set_text(lv_label_create(button), "Change");
    lv_obj_add_event_cb(button, onChange, LV_EVENT_SHORT_CLICKED, ctx);
    return value_label;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    bool has_imu = device_exists_of_type(&IMU_TYPE);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* backlight = getBacklightDevice();

    auto* toolbar = lvgl_toolbar_create(parent, "Display");
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
            lv_label_set_text(brightness_label, "Brightness");

            auto* brightness_card = lvgl_card_create(main_wrapper);
            lv_obj_set_size(brightness_card, LV_PCT(100), LV_SIZE_CONTENT);

            const int32_t percent = brightnessToPercent(backlight, ctx->displaySettings.backlightDuty);
            auto* brightness_slider = lvgl_sliderbox_create(brightness_card, 0, 100, 10, percent);
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
        lv_obj_remove_flag(auto_rotate_row, LV_OBJ_FLAG_SCROLLABLE);

        auto* auto_rotate_label = lv_label_create(auto_rotate_row);
        lv_label_set_text(auto_rotate_label, "Auto-rotate");
        lv_obj_set_flex_grow(auto_rotate_label, 1);

        ctx->autoRotateSwitch = lv_switch_create(auto_rotate_row);
        lv_obj_set_state(ctx->autoRotateSwitch, LV_STATE_CHECKED, ctx->displaySettings.autoRotateEnabled);
        lv_obj_add_event_cb(ctx->autoRotateSwitch, onAutoRotateSwitch, LV_EVENT_VALUE_CHANGED, ctx);
    }

    // The card shows the manual orientation, or the sensor mounting when auto-rotate is on
    auto* orientation_card = lvgl_card_create(main_wrapper);
    lv_obj_set_size(orientation_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(orientation_card, LV_FLEX_FLOW_COLUMN);

    ctx->orientationGroup = createCardGroup(orientation_card, "Orientation");
    ctx->orientationChips = createOrientationChips(ctx->orientationGroup, ctx->displaySettings.orientation, onOrientationChipPressed, ctx);

    // Auto-rotate needs to know how the sensor is mounted relative to the display
    if (has_imu) {
        ctx->mountRotationGroup = createCardGroup(orientation_card, "Sensor mounting");
        createOrientationChips(ctx->mountRotationGroup, ctx->displaySettings.autoRotateMountRotation, onMountRotationChipPressed, ctx);
    }

    updateOrientationVisibility(ctx);

    // Screen timeout
    // Note: DisplayIdleService doesn't act on these settings for kernel-driver displays yet
    // (it only looks up the deprecated tt::hal::display::DisplayDevice), so these currently
    // just get saved without taking effect. Kept for parity/forward-compatibility.

    if (backlight != nullptr) {
        auto* timeout_wrapper = lv_obj_create(main_wrapper);
        lv_obj_set_size(timeout_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(timeout_wrapper, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(timeout_wrapper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(timeout_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(timeout_wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_remove_flag(timeout_wrapper, LV_OBJ_FLAG_SCROLLABLE);

        auto* timeout_label = lv_label_create(timeout_wrapper);
        lv_label_set_text(timeout_label, "Auto screen off");
        lv_obj_set_flex_grow(timeout_label, 1);

        ctx->timeoutSwitch = lv_switch_create(timeout_wrapper);
        if (ctx->displaySettings.backlightTimeoutEnabled) {
            lv_obj_add_state(ctx->timeoutSwitch, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(ctx->timeoutSwitch, onTimeoutSwitch, LV_EVENT_VALUE_CHANGED, ctx);

        auto* timeout_card = lvgl_card_create(main_wrapper);
        lv_obj_set_size(timeout_card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(timeout_card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flag(timeout_card, LV_OBJ_FLAG_HIDDEN, !ctx->displaySettings.backlightTimeoutEnabled);
        ctx->timeoutCard = timeout_card;

        ctx->timeoutValueLabel = createValueRow(timeout_card, "Timeout", onChangeTimeoutPressed, ctx);
        ctx->screensaverValueLabel = createValueRow(timeout_card, "Screensaver", onChangeScreensaverPressed, ctx);
        updateTimeoutLabels(ctx);
    }
}

void destroyWidgets(void* userData) {
    auto* ctx = static_cast<Context*>(userData);
    ctx->timeoutSwitch = nullptr;
    ctx->timeoutCard = nullptr;
    ctx->timeoutValueLabel = nullptr;
    ctx->screensaverValueLabel = nullptr;
    ctx->orientationGroup = nullptr;
    ctx->orientationChips = nullptr;
    ctx->autoRotateSwitch = nullptr;
    ctx->mountRotationGroup = nullptr;
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
            // Notify AutoRotate service to reload settings
            auto autoRotate = service::autorotate::findService();
            if (autoRotate) {
                autoRotate->reloadSettings();
            }
        });
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
    check(task_event_group_claim_bit(&event_group, &ctx.selectTimeoutBit) == ERROR_NONE);
    check(task_event_group_claim_bit(&event_group, &ctx.selectScreensaverBit) == ERROR_NONE);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create_ext(appInstanceId, createWidgets, destroyWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&event_group, &flags, portMAX_DELAY);

        if (flags & ctx.selectTimeoutBit) {
            startTimeoutSelection(&ctx);
        }
        if (flags & ctx.selectScreensaverBit) {
            startScreensaverSelection(&ctx);
        }

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            switch (event.type) {
                case APP_EVENT_CLOSE:
                    persistIfUpdated(ctx);
                    shouldClose = true;
                    break;
                case APP_EVENT_RESULT:
                    if (event.result.launch_id == ctx.timeoutDialogId) {
                        onTimeoutSelectionResult(&ctx, event.result.result);
                    } else if (event.result.launch_id == ctx.screensaverDialogId) {
                        onScreensaverSelectionResult(&ctx, event.result.result);
                    }
                    lvgl_lock();
                    updateTimeoutLabels(&ctx);
                    lvgl_unlock();
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

extern const ::AppManifest manifest = {
    .id = "tactility.display",
    .name = "Display",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::display
