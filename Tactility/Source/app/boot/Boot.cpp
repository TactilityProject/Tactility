#include "tactility/system_event.h"

#include <tactility/delay.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/display.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <app/event.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <lvgl_window_manager/window_manager.h>

#include <Tactility/MountPoints.h>
#include <Tactility/Paths.h>
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
    Device* display = nullptr;
    if (device_get_first_by_type(&DISPLAY_TYPE, &display) == ERROR_NONE) {
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
        } else {
            LOG_I(TAG, "No backlight for %s", display->name);
        }
        device_put(display);
    } else {
        LOG_I(TAG, "No kernel display");
    }
}

bool setupUsbBootMode() {
    if (!hal::usb::isUsbBootMode()) {
        return false;
    }

    LOG_I(TAG, "Rebooting into mass storage device mode");
    auto mode = hal::usb::getUsbBootMode();  // Get mode before reset
    hal::usb::resetUsbBootMode();
    if (mode == hal::usb::BootMode::Flash) {
        if (!hal::usb::startMassStorageWithFlash(true)) {
            LOG_E(TAG, "Unable to start flash mass storage");
            return false;
        }
    } else if (mode == hal::usb::BootMode::Sdmmc) {
        if (!hal::usb::startMassStorageWithSdmmc(true)) {
            LOG_E(TAG, "Unable to start SD mass storage");
            return false;
        }
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
    if (app_manager_find_manifest(boot_properties.launcherAppId.c_str()) == nullptr) {
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
    app_manager_start(launcher_app_id.c_str(), &launcher_instance_id);
}

void runBootSequence(TickType_t startTime) {
    LOG_I(TAG, "Starting boot sequence");

    // Give the UI some time to redraw
    // If we don't do this, various init calls will read files and block SPI IO for the display
    // This would result in a blank/black screen being shown during this phase of the boot process
    // This works with 5 ms on a T-Lora Pager, so we give it 10 ms to be safe
    delay_millis(10);

    // TODO: Support for multiple displays
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

    if (!setupUsbBootMode()) {
        LOG_I(TAG, "initFromBootApp");
        registerApps();
        waitForMinimalSplashDuration(startTime);
        startNextApp();
    }

    // This event will likely block as other systems are initialized
    // e.g. Wi-Fi reads AP configs from SD card
    LOG_I(TAG, "Publish event");
    system_event_emit(KERNEL_EVENT_BOOT_COMPLETED, nullptr, 0);
}

int32_t appMain(uint32_t appInstanceId, int argc, char* argv[]) {
    bootAppInstanceId = appInstanceId;
    const auto start_time = get_ticks();

    // Snapshot before runBootSequence() potentially clears the flag via setupUsbBootMode()
    isUsbBootSplash = hal::usb::isUsbBootMode();
    sdCardMissing = false;

    AppEventSubscription sub {};
    sub.app_instance_id = appInstanceId;
    app_event_subscribe(&sub);

    bootWindowId = window_manager_create(appInstanceId, createSplashWidgets, nullptr);

    runBootSequence(start_time);

    // Waits until app_manager_start(launcher) (or a permanent stop) tells us to give up -
    // startNextApp() above is what triggers that, via app-module's "save the previously active
    // app" policy, unless sdCardMissing halted before it.
    while (true) {
        AppEvent event {};
        if (app_event_await(&sub, &event, portMAX_DELAY) != ERROR_NONE) {
            break;
        }
        if (event.type == APP_EVENT_CLOSE) {
            app_manager_finish(appInstanceId);
            break;
        }
    }

    if (bootWindowId != 0) {
        window_manager_remove(bootWindowId);
    }
    app_event_unsubscribe(&sub);

    return 0;
}

} // namespace

extern const ::AppManifest manifest = {
    .id = "Boot",
    .name = "Boot",
    .category = APP_CATEGORY_SYSTEM,
    .location = { APP_LOCATION_MEMORY, reinterpret_cast<void*>(appMain) },
    .flags = APP_MANIFEST_FLAG_HIDDEN,
};

} // namespace
