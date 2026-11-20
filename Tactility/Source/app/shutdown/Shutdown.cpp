// SPDX-License-Identifier: Apache-2.0
#define L10N_APP "shutdown"
#include <Tactility/i18n/AppDomain.h>

#include <Tactility/app/shutdown/Shutdown.h>
#include <Tactility/lvgl/SystemMenu.h>
#include <Tactility/settings/StandbySettings.h>
#include <Tactility/standby/Standby.h>

#include <app/manifest.h>
#include <app/start.h>

#include <lvgl/fonts.h>
#include <lvgl/lvgl.h>

#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/drivers/sleep_controller.h>
#include <tactility/log.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace tt::app::shutdown {

constexpr auto* TAG = "Shutdown";

extern const ::AppManifest manifest;

namespace {

lv_obj_t* showScreen(const char* subtitleText) {
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

bool suspend() {
    auto* sleep_controller = findSleepController();
    if (sleep_controller == nullptr) {
        LOG_E(TAG, "Sleep controller not found");
        return false;
    }
    error_t error = sleep_controller_deep_sleep(sleep_controller);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Suspend failed: %s", error_to_string(error));
        return false;
    }
    return true;
}

/** Only one operation owns the screen at a time, until its recovery is done */
std::atomic<bool> running { false };

/** Shows the screen of slow-refresh displays, as the image stays on the panel after the device is off */
bool run(const char* subtitle, bool (*action)()) {
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return false;
    }

    Device* display = nullptr;
    bool is_slow_refresh = false;
    lv_obj_t* previous_screen = nullptr;
    lv_obj_t* shutdown_screen = nullptr;
    if (device_get_first_by_type(&DISPLAY_TYPE, &display) == ERROR_NONE) {
        is_slow_refresh = display_has_capability(display, DISPLAY_CAPABILITY_SLOW_REFRESH);
        if (is_slow_refresh) {
            lvgl_lock();
            previous_screen = lv_screen_active();
            shutdown_screen = showScreen(subtitle);
            auto* lvgl_display = lv_display_get_default();
            if (lvgl_display != nullptr) {
                lv_refr_now(lvgl_display);
            }
            lvgl_unlock();

            // Clears ghosting
            error_t refresh_error = display_refresh(display);
            if (refresh_error != ERROR_NONE && refresh_error != ERROR_NOT_SUPPORTED) {
                LOG_W(TAG, "Display refresh failed: %s", error_to_string(refresh_error));
            }
            // Drivers can finish drawing asynchronously after the refresh returns
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        device_put(display);
    }

    const bool success = action();
    if (!success && shutdown_screen != nullptr) {
        lvgl_lock();
        lv_screen_load(previous_screen);
        lv_obj_delete(shutdown_screen);
        lvgl_unlock();
    }
    running.store(false, std::memory_order_release);
    return success;
}

int32_t main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("usage: shutdown <standby|suspend|poweroff|menu>\n");
        return 1;
    }

    const char* mode = argv[1];
    bool success = false;
    if (strcmp(mode, MODE_STANDBY) == 0) {
        success = standby::enter(settings::standby::loadOrGetDefault());
        if (!success) {
            printf("shutdown: standby is not available\n");
        }
    } else if (strcmp(mode, MODE_SUSPEND) == 0) {
        success = supportsSuspend() && run(tr("Suspended"), suspend);
        if (!success) {
            printf("shutdown: suspend is not supported\n");
        }
    } else if (strcmp(mode, MODE_POWER_OFF) == 0) {
        success = supportsPowerOff() && run(tr("Powered off"), powerOffAll);
        if (!success) {
            printf("shutdown: power off is not supported\n");
        }
    } else if (strcmp(mode, MODE_MENU) == 0) {
        if (!lvgl_is_running()) {
            printf("shutdown: the display is not available\n");
            return 1;
        }
        lvgl_lock();
        lvgl::systemMenuTogglePower();
        lvgl_unlock();
        success = true;
    } else {
        printf("usage: shutdown <standby|suspend|poweroff|menu>\n");
        return 1;
    }
    return success ? 0 : 1;
}

} // namespace

bool supportsPowerOff() {
    bool supported = false;
    device_for_each_of_type(&POWER_SUPPLY_TYPE, &supported, [](Device* device, void* context) {
        if (device_is_ready(device) && power_supply_supports_power_off(device)) {
            *static_cast<bool*>(context) = true;
            return false;
        }
        return true;
    });
    return supported;
}

bool supportsSuspend() {
    return findSleepController() != nullptr;
}

bool supportsStandby() {
    return !standby::findWakeSources().empty();
}

void start(const char* mode) {
    AppStartContext context;
    if (app_start_context_from_id(manifest.id, &context) != ERROR_NONE) {
        return;
    }
    const char* const arguments[] = { manifest.id, mode, nullptr };
    app_start_context_set_arguments(&context, arguments);
    AppInstanceId instance_id = 0;
    app_start_with_context(&context, &instance_id);
}

extern const ::AppManifest manifest = {
    .id = "shutdown",
    .name = "shutdown",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(main) },
    .flags = APP_MANIFEST_FLAG_HEADLESS,
    // Stack size is LVGL stack size (9 kB) plus 1 kB.
    .stack = { .depth = 10240, .desired_memory_capability = 0 },
};

} // namespace tt::app::shutdown
