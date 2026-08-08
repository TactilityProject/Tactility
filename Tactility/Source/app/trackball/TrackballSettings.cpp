#include <lvgl.h>

#include <lvgl/devices/device_context.h>
#include <lvgl/devices/trackball.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/toolbar.h>

#include <tactility/drivers/trackball.h>

#include <Tactility/Assets.h>
#include <Tactility/settings/TrackballSettings.h>
#include <Tactility/Tactility.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

namespace tt::app::trackballsettings {

extern const ::AppManifest manifest;

constexpr auto* TAG = "TrackballSettings";

// Convert mode to dropdown index (dropdown order: Encoder=0, Pointer=1)
static uint32_t modeToDropdownIndex(LvglTrackballMode mode) {
    switch (mode) {
        case LVGL_TRACKBALL_MODE_ENCODER: return 0;
        case LVGL_TRACKBALL_MODE_POINTER: return 1;
    }
    return 0; // default to Encoder
}

static lv_indev_t* findFirstTrackballIndev() {
    lv_indev_t* indev = lv_indev_get_next(nullptr);
    while (indev != nullptr) {
        void* driver_data = lv_indev_get_driver_data(indev);
        if (driver_data) {
            LvglDeviceContext* context = static_cast<LvglDeviceContext*>(driver_data);
            if (context->device) {
                const DeviceType* device_type = device_get_type(context->device);
                if (device_type == &TRACKBALL_TYPE) {
                    return indev;
                }
            }
        }

        indev = lv_indev_get_next(indev);
    }
    return nullptr;
}

namespace {

struct Context {
    uint32_t appInstanceId;
    LvglTrackballSettings tbSettings;
    bool updated = false;
    // The trackball indev currently bound by lvgl_devices_attach() at LVGL startup, if any -
    // there's at most one at a time (see devices.c), so "first active trackball device" reduces
    // to whatever is already attached.
    lv_indev_t* trackballIndev = nullptr;
    lv_obj_t* switchTrackball = nullptr;
    lv_obj_t* trackballModeDropdown = nullptr;
    lv_obj_t* encoderSensitivitySlider = nullptr;
    lv_obj_t* pointerSensitivitySlider = nullptr;
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

void applyLive(Context* ctx) {
    if (ctx->trackballIndev == nullptr) {
        return;
    }
    lvgl_lock();
    lvgl_trackball_set_settings(ctx->trackballIndev, &ctx->tbSettings);
    if (ctx->tbSettings.mode == LVGL_TRACKBALL_MODE_POINTER) {
        lvgl_trackball_set_cursor_image(ctx->trackballIndev, TT_ASSETS_UI_CURSOR);
    }
    lvgl_unlock();
}

void onTrackballSwitch(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    bool enabled = lv_obj_has_state(ctx->switchTrackball, LV_STATE_CHECKED);
    ctx->tbSettings.enabled = enabled;
    ctx->updated = true;
    applyLive(ctx);

    // Enable/disable controls based on trackball state
    if (enabled) {
        if (ctx->trackballModeDropdown) lv_obj_clear_state(ctx->trackballModeDropdown, LV_STATE_DISABLED);
        if (ctx->encoderSensitivitySlider) lv_obj_clear_state(ctx->encoderSensitivitySlider, LV_STATE_DISABLED);
        if (ctx->pointerSensitivitySlider) lv_obj_clear_state(ctx->pointerSensitivitySlider, LV_STATE_DISABLED);
    } else {
        if (ctx->trackballModeDropdown) lv_obj_add_state(ctx->trackballModeDropdown, LV_STATE_DISABLED);
        if (ctx->encoderSensitivitySlider) lv_obj_add_state(ctx->encoderSensitivitySlider, LV_STATE_DISABLED);
        if (ctx->pointerSensitivitySlider) lv_obj_add_state(ctx->pointerSensitivitySlider, LV_STATE_DISABLED);
    }
}

void onTrackballModeChanged(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    uint32_t selected = lv_dropdown_get_selected(ctx->trackballModeDropdown);

    // Validate selection matches expected enum values (dropdown order: Encoder=0, Pointer=1)
    LvglTrackballMode mode;
    switch (selected) {
        case 0: mode = LVGL_TRACKBALL_MODE_ENCODER; break;
        case 1: mode = LVGL_TRACKBALL_MODE_POINTER; break;
        default: return; // Invalid selection, ignore
    }

    ctx->tbSettings.mode = mode;
    ctx->updated = true;

    // Apply mode change immediately
    applyLive(ctx);
}

void onEncoderSensitivityChanged(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    int32_t value = lv_slider_get_value(ctx->encoderSensitivitySlider);
    ctx->tbSettings.encoder_sensitivity = static_cast<uint8_t>(value);
    ctx->updated = true;

    // Apply immediately
    applyLive(ctx);
}

void onPointerSensitivityChanged(lv_event_t* e) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(e));
    int32_t value = lv_slider_get_value(ctx->pointerSensitivitySlider);
    ctx->tbSettings.pointer_sensitivity = static_cast<uint8_t>(value);
    ctx->updated = true;

    // Apply immediately
    applyLive(ctx);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    ctx->tbSettings = settings::trackball::loadOrGetDefault();
    auto ui_density = lvgl_get_ui_density();
    ctx->updated = false;
    ctx->trackballIndev = findFirstTrackballIndev();

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 0, LV_STATE_DEFAULT);

    lv_obj_t* toolbar = lvgl_toolbar_create(parent, "Trackball");
    // The global toolbar nav callback only knows how to stop old-model apps.
    lvgl_toolbar_set_nav_action(toolbar, LV_SYMBOL_CLOSE, onBackPressed, ctx);

    if (ctx->trackballIndev == nullptr) {
        auto* wrapper = lv_obj_create(parent);
        lv_obj_set_width(wrapper, LV_PCT(100));
        lv_obj_set_flex_grow(wrapper, 1);
        lv_obj_set_flex_flow(wrapper, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        auto* label = lv_label_create(wrapper);
        lv_label_set_text(label, "No trackball device found");
        return;
    }

    // The live indev may still be running with lvgl_trackball_settings_get_default() (it's
    // bound at LVGL startup before persisted settings are known) - bring it in line with what
    // this screen is about to display.
    applyLive(ctx);

    ctx->switchTrackball = lvgl_toolbar_add_switch_action(toolbar);
    lv_obj_add_event_cb(ctx->switchTrackball, onTrackballSwitch, LV_EVENT_VALUE_CHANGED, ctx);
    if (ctx->tbSettings.enabled) lv_obj_add_state(ctx->switchTrackball, LV_STATE_CHECKED);

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

    ctx->trackballModeDropdown = lv_dropdown_create(tb_mode_wrapper);
    lv_dropdown_set_options(ctx->trackballModeDropdown, "Encoder\nPointer");
    lv_obj_align(ctx->trackballModeDropdown, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_dropdown_set_selected(ctx->trackballModeDropdown, modeToDropdownIndex(ctx->tbSettings.mode));
    lv_obj_add_event_cb(ctx->trackballModeDropdown, onTrackballModeChanged, LV_EVENT_VALUE_CHANGED, ctx);

    // Disable dropdown if trackball is disabled
    if (!ctx->tbSettings.enabled) {
        lv_obj_add_state(ctx->trackballModeDropdown, LV_STATE_DISABLED);
    }

    // Encoder sensitivity slider
    auto* enc_sens_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(enc_sens_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(enc_sens_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(enc_sens_wrapper, 0, LV_STATE_DEFAULT);
    if (ui_density != LVGL_UI_DENSITY_COMPACT) {
        lv_obj_set_style_pad_ver(enc_sens_wrapper, 4, LV_STATE_DEFAULT);
    }

    auto* enc_sens_label = lv_label_create(enc_sens_wrapper);
    lv_label_set_text(enc_sens_label, "Encoder Speed");
    lv_obj_align(enc_sens_label, LV_ALIGN_LEFT_MID, 0, 0);

    ctx->encoderSensitivitySlider = lv_slider_create(enc_sens_wrapper);
    lv_slider_set_range(ctx->encoderSensitivitySlider, 1, 10);
    lv_slider_set_value(ctx->encoderSensitivitySlider, ctx->tbSettings.encoder_sensitivity, LV_ANIM_OFF);
    lv_obj_set_width(ctx->encoderSensitivitySlider, LV_PCT(50));
    lv_obj_align(ctx->encoderSensitivitySlider, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(ctx->encoderSensitivitySlider, onEncoderSensitivityChanged, LV_EVENT_VALUE_CHANGED, ctx);

    if (!ctx->tbSettings.enabled) {
        lv_obj_add_state(ctx->encoderSensitivitySlider, LV_STATE_DISABLED);
    }

    // Pointer sensitivity slider
    auto* ptr_sens_wrapper = lv_obj_create(main_wrapper);
    lv_obj_set_size(ptr_sens_wrapper, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(ptr_sens_wrapper, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ptr_sens_wrapper, 0, LV_STATE_DEFAULT);
    if (ui_density != LVGL_UI_DENSITY_COMPACT) {
        lv_obj_set_style_pad_ver(ptr_sens_wrapper, 4, LV_STATE_DEFAULT);
    }

    auto* ptr_sens_label = lv_label_create(ptr_sens_wrapper);
    lv_label_set_text(ptr_sens_label, "Pointer Speed");
    lv_obj_align(ptr_sens_label, LV_ALIGN_LEFT_MID, 0, 0);

    ctx->pointerSensitivitySlider = lv_slider_create(ptr_sens_wrapper);
    lv_slider_set_range(ctx->pointerSensitivitySlider, 1, 10);
    lv_slider_set_value(ctx->pointerSensitivitySlider, ctx->tbSettings.pointer_sensitivity, LV_ANIM_OFF);
    lv_obj_set_width(ctx->pointerSensitivitySlider, LV_PCT(50));
    lv_obj_align(ctx->pointerSensitivitySlider, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(ctx->pointerSensitivitySlider, onPointerSensitivityChanged, LV_EVENT_VALUE_CHANGED, ctx);

    if (!ctx->tbSettings.enabled) {
        lv_obj_add_state(ctx->pointerSensitivitySlider, LV_STATE_DISABLED);
    }
}

// Mirrors the old onHide() behaviour: persist the settings (regardless of whether the app is
// giving up its thread for a save/resume cycle, or closing for good) whenever they changed.
void persistIfUpdated(Context& ctx) {
    if (ctx.updated) {
        const auto copy = ctx.tbSettings;
        getMainDispatcher().dispatch([copy]{ settings::trackball::save(copy); });
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
    .id = "TrackballSettings",
    .name = "Trackball",
    .category = APP_CATEGORY_SETTINGS,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) }
};

}
