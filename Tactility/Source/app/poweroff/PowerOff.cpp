#include "Tactility/Tactility.h"
#include "tactility/drivers/display.h"

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl.h>
#include <lvgl/devices/indev.h>
#include <lvgl/fonts.h>
#include <lvgl/lvgl.h>
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/drivers/sleep_controller.h>
#include <tactility/log.h>

namespace tt::app::poweroff {

constexpr auto* TAG = "PowerOff";

extern const ::AppManifest manifest;

namespace {

struct Context {
    uint32_t appInstanceId;
};


lv_obj_t* showPoweredOffScreen(const char* subtitleText) {
    auto* screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto* title = lv_label_create(screen);
    lv_label_set_text(title, "Tactility");
    lv_obj_set_style_text_font(title, lvgl_get_text_font(FONT_SIZE_LARGE), 0);
    lv_obj_set_style_text_color(title, lv_color_black(), 0);

    auto* subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, subtitleText);
    lv_obj_set_style_text_color(subtitle, lv_color_black(), 0);

    lv_screen_load(screen);
    return screen;
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

Device* findSleepController() {
    Device* found = nullptr;
    device_for_each_of_type(&SLEEP_CONTROLLER_TYPE, &found, [](Device* device, void* context) {
        if (device_is_ready(device)) {
            *static_cast<Device**>(context) = device;
            return false;
        }
        return true;
    });
    return found;
}

bool powerOffAll() {
    bool any_succeeded = false;
    device_for_each_of_type(&POWER_SUPPLY_TYPE, &any_succeeded, [](Device* device, void* context) {
        if (device_is_ready(device) && power_supply_supports_power_off(device)) {
            error_t error = power_supply_power_off(device);
            if (error == ERROR_NONE) {
                *static_cast<bool*>(context) = true;
            } else {
                LOG_E(TAG, "Power off failed for %s: %s", device->name, error_to_string(error));
            }
        }
        return true;
    });
    return any_succeeded;
}

bool deepSleep() {
    auto* sleep_controller = findSleepController();
    if (sleep_controller == nullptr) {
        LOG_E(TAG, "Sleep controller not found");
        return false;
    }
    error_t error = sleep_controller_deep_sleep(sleep_controller);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Deep sleep failed: %s", error_to_string(error));
        return false;
    }
    return true;
}

void runWithShutdownScreen(const char* subtitle, bool (*action)()) {
    Device* display;
    error_t error = device_get_first_by_type(&DISPLAY_TYPE, &display);
    // TODO: remove this logic path when all displays have been migrated to kernel display drivers
    if (error != ERROR_NONE) {
        action();
        return;
    }

    bool is_slow_refresh = display_has_capability(display, DISPLAY_CAPABILITY_SLOW_REFRESH);
    lv_obj_t* previous_screen = nullptr;
    lv_obj_t* shutdown_screen = nullptr;
    if (is_slow_refresh) {
        auto* lvgl_display = lv_display_get_default();
        previous_screen = lv_screen_active();
        shutdown_screen = showPoweredOffScreen(subtitle);
        if (lvgl_display != nullptr) {
            lv_refr_now(lvgl_display);
        }
    }

    getMainDispatcher().dispatch([display, is_slow_refresh, action, previous_screen, shutdown_screen] {
        if (is_slow_refresh) {
            // Clears ghosting, as the image stays on the panel after powering off
            error_t refresh_error = display_refresh(display);
            if (refresh_error != ERROR_NONE && refresh_error != ERROR_NOT_SUPPORTED) {
                LOG_W(TAG, "Display refresh failed: %s", error_to_string(refresh_error));
            }
            // Drivers can finish drawing asynchronously after the refresh returns
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        device_put(display);
        if (!action() && shutdown_screen != nullptr) {
            lvgl_lock();
            lv_screen_load(previous_screen);
            lv_obj_delete(shutdown_screen);
            lvgl_unlock();
        }
    });
}

void onYesPressed(lv_event_t* /*event*/) {
    if (!anyDeviceSupportsPowerOff()) {
        return;
    }
    runWithShutdownScreen("Powered off", powerOffAll);
}

void onSleepPressed(lv_event_t* /*event*/) {
    if (findSleepController() == nullptr) {
        return;
    }
    runWithShutdownScreen("Sleeping", deepSleep);
}

void onNoPressed(lv_event_t* event) {
    auto* ctx = static_cast<Context*>(lv_event_get_user_data(event));
    app_event_emit_close(ctx->appInstanceId);
}

void createWidgets(lv_obj_t* parent, void* userData) {
    auto* ctx = static_cast<Context*>(userData);

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    bool supports_power_off = anyDeviceSupportsPowerOff();
    bool supports_sleep = findSleepController() != nullptr;

    auto* label = lv_label_create(parent);
    lv_label_set_text(label, supports_power_off ? "Power off?" : "Sleep?");
    lv_obj_set_style_text_font(label, lvgl_get_text_font(FONT_SIZE_LARGE), 0);

    auto* button_wrapper = lv_obj_create(parent);
    lv_obj_set_flex_flow(button_wrapper, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(button_wrapper, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_border_width(button_wrapper, 0, 0);
    lv_obj_set_flex_align(button_wrapper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Ensures showing selection state for keyboard/encoder devices
    lv_obj_set_style_pad_all(button_wrapper, 0, LV_ALIGN_DEFAULT);
    lv_obj_set_style_pad_column(button_wrapper, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_ver(button_wrapper, 6, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(button_wrapper, 6, LV_STATE_DEFAULT);

    lv_obj_t* first_button = nullptr;
    if (supports_power_off) {
        auto* yes_button = lv_button_create(button_wrapper);
        auto* yes_label = lv_label_create(yes_button);
        lv_label_set_text(yes_label, "Yes");
        lv_obj_add_event_cb(yes_button, onYesPressed, LV_EVENT_SHORT_CLICKED, nullptr);
        first_button = yes_button;
    }

    if (supports_sleep) {
        auto* sleep_button = lv_button_create(button_wrapper);
        auto* sleep_label = lv_label_create(sleep_button);
        lv_label_set_text(sleep_label, "Sleep");
        lv_obj_add_event_cb(sleep_button, onSleepPressed, LV_EVENT_SHORT_CLICKED, nullptr);
        if (first_button == nullptr) {
            first_button = sleep_button;
        }
    }

    auto* no_button = lv_button_create(button_wrapper);
    auto* no_label = lv_label_create(no_button);
    lv_label_set_text(no_label, "No");
    lv_obj_add_event_cb(no_button, onNoPressed, LV_EVENT_SHORT_CLICKED, ctx);

    if (first_button != nullptr && !lvgl_indev_exists(LV_INDEV_TYPE_POINTER)) {
        lv_group_focus_obj(first_button);
        lv_obj_add_state(first_button, LV_STATE_FOCUS_KEY);
    }
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    Context ctx {};
    ctx.appInstanceId = appInstanceId;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    WindowId window = window_manager_create(appInstanceId, createWidgets, &ctx);

    bool shouldClose = false;
    while (!shouldClose) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
        switch (event.type) {
            case APP_EVENT_CLOSE:
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
    .id = "tactility.poweroff",
    .name = "Power Off",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = {}
};

} // namespace
