#include "Tactility/app/alertdialog/AlertDialog.h"
#include "tactility/memory.h"
#include "tactility/system_event.h"

#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/display.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/scheduler.h>
#include <app/start.h>

#include <lvgl_window_manager/window_manager.h>

#include <Tactility/DeprecatedPaths.h>
#include <Tactility/MountPoints.h>
#include <Tactility/TactilityPrivate.h>
#include <Tactility/hal/usb/Usb.h>
#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/settings/BootSettings.h>
#include <Tactility/settings/DisplaySettings.h>

#include <lvgl.h>

#include <atomic>
#include <format>

#ifdef ESP_PLATFORM
#include <Tactility/app/crashdiagnostics/CrashDiagnostics.h>
#include <Tactility/PanicHandler.h>
#include <esp_system.h>
#include <sdkconfig.h>
#else
#define CONFIG_TT_SPLASH_DURATION 0
#endif

namespace tt::app::boot {

constexpr auto* TAG = "Boot";

extern const ::AppManifest manifest;

namespace {

// Snapshot of hal::usb::isUsbBootMode(), taken before boot work starts and potentially clears
// the underlying flag via setupUsbBootMode()/resetUsbBootMode(). createSplashWidgets() reads
// this instead of the live flag to avoid a race between the two.
std::atomic<bool> isUsbBootSplash = false;

// Set when CONFIG_TT_USER_DATA_LOCATION_SD is defined but no SD card is mounted. Switches the
// window to an error screen and halts before starting the next app.
std::atomic<bool> sdCardMissing = false;

// Set when setupUsbBootMode() fails
std::atomic<bool> usbBootModeFailed = false;
std::string usbBootModeFailedReason;

uint32_t bootAppInstanceId = 0;
WindowId bootWindowId = 0;

#ifdef ESP_PLATFORM
constexpr auto PARTITION_PREFIX = std::string("/");
#else
constexpr auto PARTITION_PREFIX = std::string("");
#endif

// Equivalent of AppPaths::getAssetsPath() for the internal "Boot" app id, without needing a
// live AppContext (which this app no longer has under the new app-module model).
std::string getBootAssetsPath(const std::string& childPath) {
    return std::format("{}{}/app/Boot/assets/{}", PARTITION_PREFIX, file::SYSTEM_PARTITION_NAME, childPath);
}

void setupDisplay() {
    // TODO: Support for multiple displays

    Device* display = nullptr;
    if (device_get_first_by_type(&DISPLAY_TYPE, &display) != ERROR_NONE) {
        LOG_I(TAG, "No kernel display");
        return;
    }

    // Set backlight brightness
    Device* backlight;
    if (display_get_backlight(display, &backlight) == ERROR_NONE) {
        if (!device_is_ready(backlight)) {
            if (device_start(backlight) != ERROR_NONE) {
                LOG_E(TAG, "Failed to start %s", backlight->name);
            }
        }

        settings::display::DisplaySettings settings;
        if (settings::display::load(settings)) {
        } else {
            settings = settings::display::getDefault();
        }

        if (backlight_set_brightness(backlight, settings.backlightDuty) == ERROR_NONE) {
            LOG_I(TAG, "Backlight for %s set to %d", display->name, settings.backlightDuty);
        } else {
            LOG_E(TAG, "Failed to set brightness of %s", backlight->name);
        }
        device_put(backlight);
    } else {
        LOG_I(TAG, "No backlight for %s", display->name);
    }

    device_put(display);
}

// Returns true if either the MSC screen or the failure screen now owns the display, in which
// case the caller must not proceed to the normal boot path (registerApps()/startNextApp()).
bool setupUsbBootMode() {
    if (!hal::usb::isUsbBootMode()) {
        return false;
    }

    LOG_I(TAG, "Rebooting into mass storage device mode");
    auto mode = hal::usb::getUsbBootMode();  // Get mode before reset
    hal::usb::resetUsbBootMode();
    delay_millis(3000);

    bool started = false;
    if (mode == hal::usb::BootMode::Flash) {
        started = hal::usb::startMassStorageWithFlash(true);
    } else if (mode == hal::usb::BootMode::Sdmmc) {
        started = hal::usb::startMassStorageWithSdmmc(true);
    }

    if (!started) {
        usbBootModeFailedReason = hal::usb::getLastError();
        usbBootModeFailed = true;
    }

    return true;
}

void waitForMinimalSplashDuration(TickType_t startTime) {
    const auto end_time = get_ticks();
    const auto ticks_passed = end_time - startTime;
    constexpr auto minimum_ticks = (CONFIG_TT_SPLASH_DURATION / portTICK_PERIOD_MS);
    if (minimum_ticks > ticks_passed) {
        delay_ticks(minimum_ticks - ticks_passed);
    }
}

std::string getLauncherAppId() {
    settings::BootSettings boot_properties;
    // When boot.properties hasn't been overridden, return default
    if (!settings::loadBootSettings(boot_properties)) {
        return CONFIG_TT_LAUNCHER_APP_ID;
    }

    // When boot properties didn't specify an override, return default
    if (boot_properties.launcherAppId.empty()) {
        LOG_E(TAG, "Failed to load launcher configuration, or launcher not configured");
        return CONFIG_TT_LAUNCHER_APP_ID;
    }

    // If the app in the boot.properties does not exist, return default
    AppManifest manifest;
    if (app_manager_find_manifest(boot_properties.launcherAppId.c_str(), &manifest) != ERROR_NONE) {
        LOG_E(TAG, "Launcher app %s not found", boot_properties.launcherAppId.c_str());
        return CONFIG_TT_LAUNCHER_APP_ID;
    }

    // The boot.properties launcher app id is valid
    return boot_properties.launcherAppId;
}

int getSmallestDimension() {
    auto* display = lv_display_get_default();
    int width = lv_display_get_horizontal_resolution(display);
    int height = lv_display_get_vertical_resolution(display);
    return std::min(width, height);
}

void createSplashWidgets(lv_obj_t* root, void*) {
    lvgl::obj_set_style_bg_blacken(root);
    lv_obj_set_style_border_width(root, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(root, 0, LV_STATE_DEFAULT);

    auto* image = lv_image_create(root);
    lv_obj_set_size(image, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);

    const char* logo;
    // TODO: Replace with automatic asset buckets like on Android
    if (getSmallestDimension() < 150) { // e.g. Cardputer
        logo = isUsbBootSplash ? "logo_usb.png" : "logo_small.png";
    } else {
        logo = isUsbBootSplash ? "logo_usb.png" : "logo.png";
    }
    const auto logo_path = lvgl::PATH_PREFIX + getBootAssetsPath(logo);
    LOG_I(TAG, "%s", logo_path.c_str());
    lv_image_set_src(image, logo_path.c_str());

#ifdef ESP_PLATFORM
    if (isUsbBootSplash) {
        auto* button = lv_button_create(root);
        lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -16);
        auto* label = lv_label_create(button);
        lv_label_set_text(label, "Return to OS");
        lv_obj_add_event_cb(button, [](lv_event_t*) {
            hal::usb::stop();
            esp_restart();
        }, LV_EVENT_SHORT_CLICKED, nullptr);
    }
#endif
}

void createSdCardMissingWidgets(lv_obj_t* root, void*) {
    lvgl::obj_set_style_bg_blacken(root);
    lv_obj_set_style_border_width(root, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(root, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto* label = lv_label_create(root);
    lv_label_set_text(label, "SD card not found.\nPlease insert one and reboot.");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_STATE_DEFAULT);

    auto* button = lv_button_create(root);
    lv_obj_set_style_margin_top(button, 16, LV_STATE_DEFAULT);
    auto* button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Reboot");
    lv_obj_add_event_cb(button, [](lv_event_t*) {
#ifdef ESP_PLATFORM
        esp_restart();
#endif
    }, LV_EVENT_SHORT_CLICKED, nullptr);
}

// Replaces the splash with a self-contained error screen (no dependency on the old alertdialog
// app - this app has no parent in the old App stack to deliver a result back to).
void showSdCardMissingScreen() {
    if (bootWindowId != 0) {
        window_manager_remove(bootWindowId);
    }
    bootWindowId = window_manager_create(bootAppInstanceId, createSdCardMissingWidgets, nullptr);
}

void createUsbBootModeFailedWidgets(lv_obj_t* root, void*) {
    lvgl::obj_set_style_bg_blacken(root);
    lv_obj_set_style_border_width(root, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(root, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto* label = lv_label_create(root);
    lv_label_set_text(label, std::format("Failed to start USB mass storage:\n{}", usbBootModeFailedReason).c_str());
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_STATE_DEFAULT);

    auto* button = lv_button_create(root);
    lv_obj_set_style_margin_top(button, 16, LV_STATE_DEFAULT);
    auto* button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Reboot");
    lv_obj_add_event_cb(button, [](lv_event_t*) {
#ifdef ESP_PLATFORM
        esp_restart();
#endif
    }, LV_EVENT_SHORT_CLICKED, nullptr);
}

void showUsbBootModeFailedScreen() {
    if (bootWindowId != 0) {
        window_manager_remove(bootWindowId);
    }
    bootWindowId = window_manager_create(bootAppInstanceId, createUsbBootModeFailedWidgets, nullptr);
}

void startNextApp() {
    if (sdCardMissing) {
        showSdCardMissingScreen();
        return;
    }

#ifdef ESP_PLATFORM
    if (esp_reset_reason() == ESP_RST_PANIC) {
        crashdiagnostics::start(); // fire-and-forget; no result expected back
        return;
    }
#endif

    auto launcher_app_id = getLauncherAppId();
    uint32_t launcher_instance_id = 0;
    app_start(launcher_app_id.c_str(), 0, nullptr, &launcher_instance_id);
}

void runBootSequence(TickType_t startTime) {
    LOG_I(TAG, "Starting boot sequence");

    // Give the UI some time to redraw
    // If we don't do this, various init calls will read files and block SPI IO for the display
    // This would result in a blank/black screen being shown during this phase of the boot process
    // This works with 5 ms on a T-Lora Pager, so we give it 10 ms to be safe
    delay_millis(10);

    LOG_I(TAG, "Setup display");
    setupDisplay();
    LOG_I(TAG, "Prepare file systems");
    prepareFileSystems();

#ifdef CONFIG_TT_USER_DATA_LOCATION_SD
    std::string sd_path;
    if (!findFirstMountedSdCardPath(sd_path)) {
        LOG_E(TAG, "SD card not found");
        sdCardMissing = true;
    }
#endif

    if (setupUsbBootMode()) {
        if (usbBootModeFailed) {
            showUsbBootModeFailedScreen();
        }
        // Stay open: the splash's "Return to OS" button or the failure screen's "Reboot" button.
        return;
    }

    registerApps();
    waitForMinimalSplashDuration(startTime);
    startNextApp();

    if (sdCardMissing) {
        // Stay open: the error screen's "Reboot" button is this app's only way to leave here.
        return;
    }

    // This event will likely block as other systems are initialized
    // e.g. Wi-Fi reads AP configs from SD card
    LOG_I(TAG, "Publish event");
    system_event_emit(KERNEL_EVENT_BOOT_COMPLETED, nullptr, 0);

    app_event_emit_close(app_scheduler_current_app_id());
}

int32_t appMain(int argc, char* argv[]) {
    uint32_t appInstanceId = app_scheduler_current_app_id();
    bootAppInstanceId = appInstanceId;
    const auto start_time = get_ticks();

    // Snapshot before runBootSequence() potentially clears the flag via setupUsbBootMode()
    isUsbBootSplash = hal::usb::isUsbBootMode();
    sdCardMissing = false;

    TaskEventGroup event_group {};
    task_event_group_construct(&event_group);

    AppEventSubscription sub {};
    check(app_event_subscribe(&sub, &event_group) == ERROR_NONE);

    bootWindowId = window_manager_create(appInstanceId, createSplashWidgets, nullptr);

    runBootSequence(start_time);

    // Waits until app_start(launcher) (or a permanent stop) tells us to give up -
    // startNextApp() above is what triggers that, via app-module's "save the previously active
    // app" policy, unless sdCardMissing halted before it.
    while (true) {
        task_event_group_wait_any(&event_group, nullptr, portMAX_DELAY);

        bool shouldClose = false;
        AppEvent event {};
        while (app_event_poll(&sub, &event) == ERROR_NONE) {
            if (event.type == APP_EVENT_CLOSE) {
                shouldClose = true;
                break;
            }
        }
        if (shouldClose) break;
    }

    if (bootWindowId != 0) {
        window_manager_remove(bootWindowId);
    }
    check(app_event_unsubscribe(&sub) == ERROR_NONE);
    task_event_group_destruct(&event_group);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "tactility.boot",
    .name = "Boot",
    .category = APP_CATEGORY_SYSTEM,
    .location = { .type = APP_LOCATION_MEMORY, .location = reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
    .stack = { .depth = 4096, .desired_memory_capability = MEMORY_CAPABILITY_INTERNAL }
};

} // namespace
