#include <lvgl.h>

#include <lvgl/lvgl.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/chip.h>
#include <lvgl/widgets/sliderbox.h>
#include <lvgl/widgets/toolbar.h>

#include <tactility/check.h>
#include <tactility/drivers/trackball.h>
#include <tactility/file_ops.h>

#include <lvgl/ui_assets.h>
#include <Tactility/settings/TrackballSettings.h>
#include <Tactility/Tactility.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <new>

namespace tt::app::trackballsettings {

extern const ::AppManifest manifest;

constexpr auto* TAG = "TrackballSettings";

struct ModeOption {
    TrackballMode mode;
    const char* name;
};

// The order of the mode chips
constexpr ModeOption MODE_OPTIONS[] = {
    { TRACKBALL_MODE_KEYBOARD, "Keys" },
    { TRACKBALL_MODE_MOUSE, "Pointer" },
};

namespace {

struct Context {
    uint32_t appInstanceId;
    TrackballSettings tbSettings;
    bool updated = false;
    Device* trackball = nullptr;
    TaskEventGroup* eventGroup = nullptr;
    uint32_t applyBit = 0;
    lv_obj_t* switchTrackball = nullptr;
    // Shown while the trackball is enabled
    lv_obj_t* settingsCard = nullptr;
    lv_obj_t* modeChips = nullptr;
    // Shown in keys mode
    lv_obj_t* keySensitivityGroup = nullptr;
    lv_obj_t* keySensitivitySlider = nullptr;
    // Shown in pointer mode
    lv_obj_t* pointerSensitivityGroup = nullptr;
    lv_obj_t* pointerSensitivitySlider = nullptr;
};


void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

/** Applies the settings on the app's task: changing the mode replaces the trackball's child device, which can be the device that sent the event. */
void applyLive(Context* ctx) {
    task_event_group_signal(ctx->eventGroup, ctx->applyBit);
}

/** Shows the settings of the enabled trackball and its mode */
void updateWidgets(Context* ctx) {
    lv_obj_set_hidden(ctx->settingsCard, !ctx->tbSettings.enabled);
    const uint32_t chip_count = lv_obj_get_child_count(ctx->modeChips);
    for (uint32_t i = 0; i < chip_count; i++) {
        lv_obj_set_state(lv_obj_get_child(ctx->modeChips, static_cast<int32_t>(i)), LV_STATE_CHECKED, MODE_OPTIONS[i].mode == ctx->tbSettings.mode);
    }
    lv_obj_set_hidden(ctx->keySensitivityGroup, ctx->tbSettings.mode != TRACKBALL_MODE_KEYBOARD);
    lv_obj_set_hidden(ctx->pointerSensitivityGroup, ctx->tbSettings.mode != TRACKBALL_MODE_MOUSE);
}

void onTrackballSwitch(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    ctx->tbSettings.enabled = lv_obj_has_state(ctx->switchTrackball, LV_STATE_CHECKED);
    ctx->updated = true;
    applyLive(ctx);
    updateWidgets(ctx);
}

void onModeChipPressed(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    ctx->tbSettings.mode = MODE_OPTIONS[lv_obj_get_index(lv_event_get_target_obj(e))].mode;
    ctx->updated = true;
    // Apply mode change immediately
    applyLive(ctx);
    updateWidgets(ctx);
}

void onKeySensitivityChanged(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    int32_t value = lvgl_sliderbox_get_value(ctx->keySensitivitySlider);
    ctx->tbSettings.key_sensitivity = static_cast<uint8_t>(value);
    ctx->updated = true;

    // Apply immediately
    applyLive(ctx);
}

void onPointerSensitivityChanged(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    int32_t value = lvgl_sliderbox_get_value(ctx->pointerSensitivitySlider);
    ctx->tbSettings.pointer_sensitivity = static_cast<uint8_t>(value);
    ctx->updated = true;

    // Apply immediately
    applyLive(ctx);
}

/** A transparent column with a title and a speed slider */
lv_obj_t* createSpeedGroup(lv_obj_t* parent, const char* title, int32_t value, lv_event_cb_t onChanged, Context* ctx, lv_obj_t** outSlider) {
    auto* group = lv_obj_create(parent);
    lv_obj_set_size(group, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(group, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(group, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(group, false);

    lv_label_set_text(lv_label_create(group), title);

    *outSlider = lvgl_sliderbox_create(group, 1, 10, 1, value);
    lv_obj_set_width(*outSlider, LV_PCT(100));
    lvgl_sliderbox_add_value_changed_cb(*outSlider, onChanged, ctx);
    return group;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);


    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    lv_obj_t* toolbar = lvgl_toolbar_create(parent, "Trackball");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    if (ctx->trackball == nullptr) {
        auto* wrapper = lv_obj_create(parent);
        lv_obj_set_style_border_width(wrapper, 0, LV_STATE_DEFAULT);
        lv_obj_set_width(wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(wrapper, 1);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        auto* card = lvgl_card_create(wrapper);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_label_set_text(lv_label_create(card), "No trackball device found.");
        return;
    }

    ctx->switchTrackball = lvgl_toolbar_add_switch_action(toolbar);
    lv_obj_add_event_cb(ctx->switchTrackball, onTrackballSwitch, LV_EVENT_VALUE_CHANGED, ctx);
    if (ctx->tbSettings.enabled) lv_obj_add_state(ctx->switchTrackball, LV_STATE_CHECKED);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(main_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    ctx->settingsCard = lvgl_card_create(main_wrapper);
    lv_obj_set_size(ctx->settingsCard, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctx->settingsCard, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ctx->settingsCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Mode: the chips are centered while they fit, and scroll when they don't
    ctx->modeChips = lv_obj_create(ctx->settingsCard);
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

    // The speed of the selected mode
    ctx->keySensitivityGroup = createSpeedGroup(ctx->settingsCard, "Key speed", ctx->tbSettings.key_sensitivity, onKeySensitivityChanged, ctx, &ctx->keySensitivitySlider);
    ctx->pointerSensitivityGroup = createSpeedGroup(ctx->settingsCard, "Pointer speed", ctx->tbSettings.pointer_sensitivity, onPointerSensitivityChanged, ctx, &ctx->pointerSensitivitySlider);

    updateWidgets(ctx);
}

// Mirrors the old onHide() behaviour: persist the settings (regardless of whether the app is
// giving up its thread for a save/resume cycle, or closing for good) whenever they changed.
struct SaveSettingsJob {
    TrackballSettings settings;
};

/** File operation: it runs on the file operations task, which frees the job */
void saveSettingsInBackground(void* context) {
    auto* job = static_cast<SaveSettingsJob*>(context);
    settings::trackball::save(job->settings);
    delete job;
}

void persistIfUpdated(Context& ctx) {
    if (ctx.updated) {
        auto* job = new (::std::nothrow) SaveSettingsJob { ctx.tbSettings };
        if (job == nullptr || file_ops_queue(saveSettingsInBackground, job, nullptr) != ERROR_NONE) {
            LOG_E(TAG, "Failed to queue saving trackball settings");
            delete job;
        }
        ctx.updated = false;
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    ctx.eventGroup = &event_group;
    check(task_event_group_claim_bit(&event_group, &ctx.applyBit) == ERROR_NONE);

    if (device_get_first_active_by_type(&TRACKBALL_TYPE, &ctx.trackball) != ERROR_NONE) {
        ctx.trackball = nullptr;
    }
    // The active settings are the stored ones, or the defaults of the devicetree
    if (ctx.trackball == nullptr || trackball_get_settings(ctx.trackball, &ctx.tbSettings) != ERROR_NONE) {
        ctx.tbSettings = trackball_settings_get_default();
    }

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&event_group, &flags, portMAX_DELAY);

        if ((flags & ctx.applyBit) && ctx.trackball != nullptr) {
            const TrackballSettings settings = ctx.tbSettings;
            trackball_set_settings(ctx.trackball, &settings);
        }

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
    if (ctx.trackball != nullptr) {
        device_put(ctx.trackball);
    }
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.trackballsettings",
    .name = "Trackball",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

}
