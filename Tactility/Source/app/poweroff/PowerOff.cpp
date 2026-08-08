#include "Tactility/Tactility.h"
#include "tactility/drivers/display.h"

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl.h>
#include <lvgl/fonts.h>
#include <tactility/device.h>
#include <tactility/drivers/power_supply.h>

namespace tt::app::poweroff {

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
};


void showPoweredOffScreen() {
    auto* screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto* title = lv_label_create(screen);
    lv_label_set_text(title, "Tactility");
    lv_obj_set_style_text_font(title, lvgl_get_text_font(FONT_SIZE_LARGE), 0);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);

    auto* subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "Powered off");
    lv_obj_set_style_text_color(subtitle, lv_color_black(), 0);

    lv_screen_load(screen);
}

bool anyDeviceSupportsPowerOff() {
    bool any_supported = false;
    device_for_each_of_type(&POWER_SUPPLY_TYPE, &any_supported, [](Device* device, void* context) {
        if (device_is_ready(device) && power_supply_supports_power_off(device)) {
            *static_cast<bool*>(context) = true;
            return false;
        }
        return true;
    });
    return any_supported;
}

void onYesPressed(lv_event_t* /*event*/) {
    if (!anyDeviceSupportsPowerOff()) {
        return;
    }

    Device* display;
    error_t error = device_get_first_by_type(&DISPLAY_TYPE, &display);
    // TODO: remove this logic path when all displays have been migrated to kernel display drivers
    if (error != ERROR_NONE) {
        // No display, power off now
        device_for_each_of_type(&POWER_SUPPLY_TYPE, nullptr, [](Device* device, void* /*context*/) {
            if (device_is_ready(device) && power_supply_supports_power_off(device)) {
                power_supply_power_off(device);
            }
            return true;
        });
        return;
    }

    bool is_slow_refresh = display_has_capability(display, DISPLAY_CAPABILITY_SLOW_REFRESH);
    if (is_slow_refresh) {
        auto* lvgl_display = lv_display_get_default();
        showPoweredOffScreen();
        if (lvgl_display != nullptr) {
            lv_refr_now(lvgl_display);
        }
    }

    getMainDispatcher().dispatch([is_slow_refresh] {
        // Not necessary for LilyGO Paper S3, but other drivers with async rendering might need us to wait a bit.
        if (is_slow_refresh) {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        device_for_each_of_type(&POWER_SUPPLY_TYPE, nullptr, [](Device* device, void* /*context*/) {
            if (device_is_ready(device) && power_supply_supports_power_off(device)) {
                power_supply_power_off(device);
            }
            return true;
        });
    });
}

void onNoPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that bound-waits
    // (thread_join) for this app's own thread to finish, which needs the LVGL lock
    // (window_manager_remove()) - but this callback runs ON the LVGL task, which would
    // deadlock against itself.
    AppEvent closeEvent { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(ctx->appInstanceId, &closeEvent);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto* label = lv_label_create(parent);
    lv_label_set_text(label, "Power off?");
    lv_obj_set_style_text_font(label, lvgl_get_text_font(FONT_SIZE_LARGE), 0);

    auto* button_wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(button_wrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(button_wrapper, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(button_wrapper, 0, 0);
    lv_obj_set_flex_align(button_wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto* yes_button = lv_button_create(button_wrapper);
    auto* yes_label = lv_label_create(yes_button);
    lv_label_set_text(yes_label, "Yes");
    lv_obj_add_event_cb(yes_button, onYesPressed, LV_EVENT_SHORT_CLICKED, nullptr);

    auto* no_button = lv_button_create(button_wrapper);
    auto* no_label = lv_label_create(no_button);
    lv_label_set_text(no_label, "No");
    lv_obj_add_event_cb(no_button, onNoPressed, LV_EVENT_SHORT_CLICKED, ctx);
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
    .id = "PowerOff",
    .name = "Power Off",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
};

} // namespace
