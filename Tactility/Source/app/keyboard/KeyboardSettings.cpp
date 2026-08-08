#ifdef ESP_PLATFORM

#include <Tactility/Tactility.h>

#include <Tactility/settings/KeyboardSettings.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/device.h>
#include <tactility/drivers/backlight.h>

#include <lvgl.h>
#include <lvgl/widgets/toolbar.h>

namespace tt::app::keyboardsettings {

extern const ::AppManifest manifest;

constexpr auto* TAG = "KeyboardSettings";

// Shared timeout values: 15s, 30s, 1m, 2m, 5m, Never (0)
static constexpr uint32_t TIMEOUT_VALUES_MS[] = {15000, 30000, 60000, 120000, 300000, 0};
static constexpr size_t TIMEOUT_DEFAULT_IDX = 2; // 1 minute

static uint32_t timeoutMsToIndex(uint32_t ms) {
    for (size_t i = 0; i < sizeof(TIMEOUT_VALUES_MS) / sizeof(TIMEOUT_VALUES_MS[0]); ++i) {
        if (TIMEOUT_VALUES_MS[i] == ms) return static_cast<uint32_t>(i);
    }
    return TIMEOUT_DEFAULT_IDX;
}

static void applyKeyboardBacklight(bool enabled, uint8_t brightness) {
    // TODO: Get keyboard backlight from (optional) keyboard child device
    Device* backlight;
    if (device_get_by_name("keyboard_backlight", &backlight) == ERROR_NONE) {
        backlight_set_brightness(backlight, enabled ? brightness : 0);
        device_put(backlight);
    }
}

namespace {

struct Context {
    uint32_t appInstanceId;
    settings::keyboard::KeyboardSettings kbSettings;
    bool updated = false;
    lv_obj_t* switchBacklight = nullptr;
    lv_obj_t* sliderBrightness = nullptr;
    lv_obj_t* switchTimeoutEnable = nullptr;
    lv_obj_t* timeoutDropdown = nullptr;
};


void onBackPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void onBacklightSwitch(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    bool enabled = lv_obj_has_state(ctx->switchBacklight, LV_STATE_CHECKED);
    ctx->kbSettings.backlightEnabled = enabled;
    ctx->updated = true;
    if (ctx->sliderBrightness) {
        if (enabled) lv_obj_clear_state(ctx->sliderBrightness, LV_STATE_DISABLED);
        else lv_obj_add_state(ctx->sliderBrightness, LV_STATE_DISABLED);
    }
    applyKeyboardBacklight(enabled, ctx->kbSettings.backlightBrightness);
}

void onBrightnessChanged(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    int32_t v = lv_slider_get_value(ctx->sliderBrightness);
    ctx->kbSettings.backlightBrightness = static_cast<uint8_t>(v);
    ctx->updated = true;
    if (ctx->kbSettings.backlightEnabled) {
        applyKeyboardBacklight(true, ctx->kbSettings.backlightBrightness);
    }
}

void onTimeoutEnableSwitch(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    bool enabled = lv_obj_has_state(ctx->switchTimeoutEnable, LV_STATE_CHECKED);
    ctx->kbSettings.backlightTimeoutEnabled = enabled;
    ctx->updated = true;
    if (ctx->timeoutDropdown) {
        if (enabled) {
            lv_obj_clear_state(ctx->timeoutDropdown, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(ctx->timeoutDropdown, LV_STATE_DISABLED);
        }
    }
}

void onTimeoutChanged(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_target(event));
    uint32_t idx = lv_dropdown_get_selected(dropdown);
    if (idx < (sizeof(TIMEOUT_VALUES_MS) / sizeof(TIMEOUT_VALUES_MS[0]))) {
        ctx->kbSettings.backlightTimeoutMs = TIMEOUT_VALUES_MS[idx];
        ctx->updated = true;
    }
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    ctx->kbSettings = settings::keyboard::loadOrGetDefault();
    ctx->updated = false;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    auto* toolbar = lvgl_toolbar_create(parent, "Keyboard");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    auto* main_wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(main_wrapper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(main_wrapper, LV_PCT(100));
    lv_obj_set_flex_grow(main_wrapper, 1);

    // Keyboard backlight toggle
    auto* bl_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(bl_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(bl_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bl_wrapper, 0, LV_STATE_DEFAULT);

    auto* bl_label = lv_label_create(bl_wrapper);
    lv_label_set_text(bl_label, "Keyboard backlight");
    lv_obj_align(bl_label, LV_ALIGN_LEFT_MID, 0, 0);
    ctx->switchBacklight = lv_switch_create(bl_wrapper);
    if (ctx->kbSettings.backlightEnabled) lv_obj_add_state(ctx->switchBacklight, LV_STATE_CHECKED);
    lv_obj_align(ctx->switchBacklight, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(ctx->switchBacklight, onBacklightSwitch, LV_EVENT_VALUE_CHANGED, ctx);

    // Brightness slider
    auto* br_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(br_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(br_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(br_wrapper, 0, LV_STATE_DEFAULT);

    auto* br_label = lv_label_create(br_wrapper);
    lv_label_set_text(br_label, "Brightness");
    lv_obj_align(br_label, LV_ALIGN_LEFT_MID, 0, 0);
    ctx->sliderBrightness = lv_slider_create(br_wrapper);
    lv_obj_set_width(ctx->sliderBrightness, LV_PCT(50));
    lv_obj_align(ctx->sliderBrightness, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_slider_set_range(ctx->sliderBrightness, 0, 255);
    lv_slider_set_value(ctx->sliderBrightness, ctx->kbSettings.backlightBrightness, LV_ANIM_OFF);
    if (!ctx->kbSettings.backlightEnabled) lv_obj_add_state(ctx->sliderBrightness, LV_STATE_DISABLED);
    lv_obj_add_event_cb(ctx->sliderBrightness, onBrightnessChanged, LV_EVENT_VALUE_CHANGED, ctx);

    // Backlight timeout enable
    auto* to_enable_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(to_enable_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(to_enable_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(to_enable_wrapper, 0, LV_STATE_DEFAULT);

    auto* to_enable_label = lv_label_create(to_enable_wrapper);
    lv_label_set_text(to_enable_label, "Auto backlight off");
    lv_obj_align(to_enable_label, LV_ALIGN_LEFT_MID, 0, 0);
    ctx->switchTimeoutEnable = lv_switch_create(to_enable_wrapper);
    if (ctx->kbSettings.backlightTimeoutEnabled) lv_obj_add_state(ctx->switchTimeoutEnable, LV_STATE_CHECKED);
    lv_obj_align(ctx->switchTimeoutEnable, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(ctx->switchTimeoutEnable, onTimeoutEnableSwitch, LV_EVENT_VALUE_CHANGED, ctx);

    auto* timeout_select_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(timeout_select_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(timeout_select_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(timeout_select_wrapper, 0, LV_STATE_DEFAULT);

    auto* timeout_value_label = lv_label_create(timeout_select_wrapper);
    lv_label_set_text(timeout_value_label, "Timeout");
    lv_obj_align(timeout_value_label, LV_ALIGN_LEFT_MID, 0, 0);

    // Backlight timeout value (seconds)
    ctx->timeoutDropdown = lv_dropdown_create(timeout_select_wrapper);
    lv_dropdown_set_options(ctx->timeoutDropdown, "15 seconds\n30 seconds\n1 minute\n2 minutes\n5 minutes\nNever");
    lv_obj_align(ctx->timeoutDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(ctx->timeoutDropdown, onTimeoutChanged, LV_EVENT_VALUE_CHANGED, ctx);
    // Initialize dropdown selection from settings
    lv_dropdown_set_selected(ctx->timeoutDropdown, timeoutMsToIndex(ctx->kbSettings.backlightTimeoutMs));
    if (!ctx->kbSettings.backlightTimeoutEnabled) {
        lv_obj_add_state(ctx->timeoutDropdown, LV_STATE_DISABLED);
    }
}

// Mirrors the old onHide() behaviour: persist the settings (regardless of whether the app is
// giving up its thread for a save/resume cycle, or closing for good) whenever they changed.
void persistIfUpdated(Context& ctx) {
    if (ctx.updated) {
        const auto copy = ctx.kbSettings;
        getMainDispatcher().dispatch([copy]{ settings::keyboard::save(copy); });
        ctx.updated = false;
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
    .id = "KeyboardSettings",
    .name = "Keyboard",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

}

#endif
