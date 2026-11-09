// SPDX-License-Identifier: Apache-2.0
#include <Tactility/settings/VibrationSettings.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/vibration.h>

#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/sliderbox.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::vibrationsettings {

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
    struct Device* device = nullptr;
    settings::vibration::VibrationSettings settings {};
    struct TaskEventGroup* eventGroup = nullptr;
    uint32_t saveBit = 0;
    lv_obj_t* enabledSwitch = nullptr;
    lv_obj_t* strengthSlider = nullptr;
    lv_obj_t* strengthRow = nullptr;
    lv_obj_t* durationRow = nullptr;
    lv_obj_t* testButton = nullptr;
};

// Settings are saved by the app task, never from an LVGL callback.
void requestSave(Context* ctx) {
    task_event_group_signal(ctx->eventGroup, ctx->saveBit);
}

void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void applyEnabled(Context* ctx) {
    if (!ctx->settings.enabled && ctx->device != nullptr) {
        vibration_set_strength(ctx->device, 0.0f);
    }
    if (ctx->strengthRow != nullptr) {
        lv_obj_set_hidden(ctx->strengthRow, !ctx->settings.enabled);
    }
    if (ctx->durationRow != nullptr) {
        lv_obj_set_hidden(ctx->durationRow, !ctx->settings.enabled);
    }
    if (ctx->testButton != nullptr) {
        lv_obj_set_hidden(ctx->testButton, !ctx->settings.enabled);
    }
}

void onEnabledSwitch(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(event));
    ctx->settings.enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    applyEnabled(ctx);
    requestSave(ctx);
}

void onStrengthSlider(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* sliderBox = static_cast<lv_obj_t*>(lv_event_get_target(event));
    ctx->settings.strength = static_cast<uint8_t>(lvgl_sliderbox_get_value(sliderBox));
    requestSave(ctx);
}

void onDurationSlider(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* sliderBox = static_cast<lv_obj_t*>(lv_event_get_target(event));
    ctx->settings.duration_ms = static_cast<uint32_t>(lvgl_sliderbox_get_value(sliderBox));
    requestSave(ctx);
}

void onTestPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    if (ctx->device != nullptr && ctx->settings.enabled) {
        vibration_pulse(ctx->device, ctx->settings.duration_ms, static_cast<float>(ctx->settings.strength));
    }
}

lv_obj_t* createRow(lv_obj_t* parent) {
    auto* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);
    return row;
}

lv_obj_t* createSwitchRow(lv_obj_t* parent, const char* label, lv_event_cb_t cb, void* userData) {
    auto* row = createRow(parent);

    auto* row_label = lv_label_create(row);
    lv_label_set_text(row_label, label);
    lv_obj_set_flex_grow(row_label, 1);

    auto* sw = lv_switch_create(row);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, userData);
    return sw;
}

lv_obj_t* createSliderRow(lv_obj_t* parent, const char* label, int32_t min, int32_t max, int32_t step, int32_t initialValue, lv_event_cb_t cb, void* userData) {
    auto* row = createRow(parent);

    auto* row_label = lv_label_create(row);
    lv_label_set_text(row_label, label);
    lv_obj_set_flex_grow(row_label, 1);

    auto* sliderBox = lvgl_sliderbox_create(row, min, max, step, initialValue);
    lv_obj_set_width(sliderBox, LV_PCT(75));
    lvgl_sliderbox_add_value_changed_cb(sliderBox, cb, userData);
    return sliderBox;
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Vibration");
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_style_border_width(main_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);
    lv_obj_set_style_pad_row(main_wrapper, 8, LV_STATE_DEFAULT);

    if (ctx->device == nullptr) {
        auto* label = lv_label_create(main_wrapper);
        lv_label_set_text(label, "No vibration motor available");
        lv_obj_center(label);
        return;
    }

    auto* card = lvgl_card_create(main_wrapper);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, LV_STATE_DEFAULT);

    ctx->enabledSwitch = createSwitchRow(card, "Enabled", onEnabledSwitch, ctx);
    lv_obj_set_state(ctx->enabledSwitch, LV_STATE_CHECKED, ctx->settings.enabled);

    ctx->strengthSlider = createSliderRow(card, "Strength", 0, 100, 10, ctx->settings.strength, onStrengthSlider, ctx);
    ctx->strengthRow = lv_obj_get_parent(ctx->strengthSlider);

    auto* durationSlider = createSliderRow(
        card,
        "Duration (ms)",
        static_cast<int32_t>(settings::vibration::DURATION_MIN_MS),
        static_cast<int32_t>(settings::vibration::DURATION_MAX_MS),
        static_cast<int32_t>(settings::vibration::DURATION_STEP_MS),
        static_cast<int32_t>(ctx->settings.duration_ms),
        onDurationSlider,
        ctx
    );
    ctx->durationRow = lv_obj_get_parent(durationSlider);

    ctx->testButton = lv_button_create(main_wrapper);
    lv_obj_set_width(ctx->testButton, LV_PCT(100));
    lv_obj_set_style_pad_ver(ctx->testButton, 8, LV_STATE_DEFAULT);
    auto* test_label = lv_label_create(ctx->testButton);
    lv_label_set_text(test_label, "Test");
    lv_obj_center(test_label);
    lv_obj_add_event_cb(ctx->testButton, onTestPressed, LV_EVENT_SHORT_CLICKED, ctx);

    applyEnabled(ctx);
}

int32_t appMain(int, char**) {
    uint32_t appInstanceId = app_scheduler_current_app_id();

    Context ctx {};
    ctx.appInstanceId = appInstanceId;
    ctx.settings = settings::vibration::loadOrGetDefault();
    device_get_first_by_type(&VIBRATION_TYPE, &ctx.device);

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);
    check(task_event_group_claim_bit(&event_group, &ctx.saveBit) == ERROR_NONE);
    ctx.eventGroup = &event_group;

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    bool shouldClose = false;
    while (!shouldClose) {
        uint32_t flags = 0;
        task_event_group_wait_any(&event_group, &flags, portMAX_DELAY);

        if ((flags & ctx.saveBit) != 0) {
            settings::vibration::save(ctx.settings);
        }

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
        }
    }

    task_event_group_release_bit(&event_group, ctx.saveBit);
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);
    window_manager_remove(window);

    if (ctx.device != nullptr) {
        vibration_set_strength(ctx.device, 0.0f);
        device_put(ctx.device);
        ctx.device = nullptr;
    }

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.vibrationsettings",
    .name = "Vibration",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = 0,
    .stack = {}
};

} // namespace tt::app::vibrationsettings
