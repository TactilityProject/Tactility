#include <Tactility/app/boot/BootInit.h>

#include "Tactility/bluetooth/Bluetooth.h"
#include "Tactility/hal/SdCard.h"
#include "Tactility/network/NtpPrivate.h"
#include "Tactility/settings/TimePrivate.h"

#include <tactility/delay.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/display.h>
#include <tactility/log.h>
#include <tactility/system_event.h>
#include <tactility/time.h>

#include <app/manager.h>
#include <app/manifest.h>
#include <app/start.h>

#include <Tactility/DeprecatedPaths.h>
#include <Tactility/MountPoints.h>
#include <Tactility/TactilityPrivate.h>
#include <Tactility/app/boot/BootScreen.h>
#include <Tactility/hal/usb/Usb.h>
#include <Tactility/lvgl/Fonts.h>
#include <Tactility/lvgl/Lvgl.h>
#include <Tactility/lvgl/Theme.h>
#include <Tactility/settings/AppearanceSettings.h>
#include <Tactility/settings/BootSettings.h>
#include <Tactility/settings/DisplaySettings.h>

#include <format>

#ifdef ESP_PLATFORM
#include <Tactility/app/crashdiagnostics/CrashDiagnostics.h>
#include <esp_system.h>
#include <sdkconfig.h>
#else
#define CONFIG_TT_SPLASH_DURATION 0
#endif

namespace tt::app::boot {

constexpr auto* TAG = "Boot";
constexpr auto* DEFAULT_LAUNCHER_ID = "tactility.launcher";

namespace {

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

// TODO: Replace with automatic asset buckets like on Android
std::string getSizedAssetPath(const BootScreen& screen, const char* small, const char* medium, const char* large) {
    const int smallest_dimension = screen.getSmallestDimension();
    if (smallest_dimension < 150) { // e.g. Cardputer
        return getBootAssetsPath(small);
    } else if (smallest_dimension >= 320) {
        return getBootAssetsPath(large);
    } else {
        return getBootAssetsPath(medium);
    }
}

std::string getLogoPath(const BootScreen& screen) {
    return getSizedAssetPath(screen, "logo_small.png", "logo.png", "logo_large.png");
}

std::string getUsbLogoPath(const BootScreen& screen) {
    return getSizedAssetPath(screen, "logo_usb_small.png", "logo_usb.png", "logo_usb_large.png");
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

enum class UsbBootResult {
    /** Not booting into USB mass storage mode */
    None,
    Started,
    Failed
};

UsbBootResult setupUsbBootMode() {
    if (!hal::usb::isUsbBootMode()) {
        return UsbBootResult::None;
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

    return started ? UsbBootResult::Started : UsbBootResult::Failed;
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
        return DEFAULT_LAUNCHER_ID;
    }

    // When boot properties didn't specify an override, return default
    if (boot_properties.launcherAppId.empty()) {
        LOG_E(TAG, "Failed to load launcher configuration, or launcher not configured");
        return DEFAULT_LAUNCHER_ID;
    }

    // If the app in the boot.properties does not exist, return default
    AppManifest manifest;
    if (app_manager_find_manifest(boot_properties.launcherAppId.c_str(), &manifest) != ERROR_NONE) {
        LOG_E(TAG, "Launcher app %s not found", boot_properties.launcherAppId.c_str());
        return DEFAULT_LAUNCHER_ID;
    }

    // The boot.properties launcher app id is valid
    return boot_properties.launcherAppId;
}

void reboot() {
#ifdef ESP_PLATFORM
    esp_restart();
#else
    LOG_W(TAG, "Reboot is not supported on this platform");
#endif
}

void waitForInputAndReboot() {
    waitForInput();
    reboot();
}

void startNextApp() {
    auto launcher_app_id = getLauncherAppId();
    uint32_t launcher_instance_id = 0;
    AppStartContext context;
    if (app_start_context_from_id(launcher_app_id.c_str(), &context) == ERROR_NONE) {
        app_start_with_context(&context, &launcher_instance_id);
    }
}

} // namespace

bool bootInit(TickType_t startTime) {
    LOG_I(TAG, "Starting boot sequence");
    const bool is_usb_boot = hal::usb::isUsbBootMode();

    auto start_time = get_millis();

    BootScreen screen;
    if (screen.begin()) {
        screen.show(is_usb_boot ? getUsbLogoPath(screen) : getLogoPath(screen), {});
    }

    LOG_I(TAG, "Init display");
    setupDisplay();

    LOG_I(TAG, "Init timezone");
    settings::initTimeZone();

    // Attempt to start all disabled SD cards (some require delayed init)
    LOG_I(TAG, "Init SDMMC");
    hal::sdcard::startAll();

    LOG_I(TAG, "Init NTP");
    network::ntp::init();

    LOG_I(TAG, "Init BLE");
    bluetooth::systemStart();

    LOG_I(TAG, "Init Services");
    registerAndStartServices();

    LOG_I(TAG, "Prepare file systems");
    prepareFileSystems();

    bool sd_card_missing = false;
#ifdef CONFIG_TT_USER_DATA_LOCATION_SD
    std::string sd_path;
    if (!findFirstMountedSdCardPath(sd_path)) {
        LOG_E(TAG, "SD card not found");
        sd_card_missing = true;
    }
#endif

    switch (setupUsbBootMode()) {
        case UsbBootResult::None:
            break;
        case UsbBootResult::Started:
#ifdef ESP_PLATFORM
            screen.show(getUsbLogoPath(screen), { getInputPrompt("return to OS") });
            waitForInput();
            hal::usb::stop();
            esp_restart();
#endif
            return false;
        case UsbBootResult::Failed:
            screen.show("", { "Failed to start USB mass storage:", hal::usb::getLastError(), getInputPrompt("reboot") });
            waitForInputAndReboot();
            return false;
    }

    registerApps();

    if (sd_card_missing) {
        screen.show("", { "SD card not found.", "Please insert one and reboot.", getInputPrompt("reboot") });
        waitForInputAndReboot();
        return false;
    }

#ifdef ESP_PLATFORM
    if (esp_reset_reason() == ESP_RST_PANIC) {
        crashdiagnostics::showCrashScreen(screen);
    }
#endif

    LOG_I(TAG, "Loading fonts");
    lvgl::loadFonts(lvgl::loadFontConfiguration());
    lvgl::configureTheme(settings::appearance::loadOrGetDefault());

    screen.end();

    size_t total_time = get_millis() - start_time;
    LOG_I(TAG, "Finished in %lu ms", static_cast<uint32_t>(total_time));

    waitForMinimalSplashDuration(startTime);

    LOG_I(TAG, "Starting LVGL");
    lvgl::start();
    startNextApp();

    // This event will likely block as other systems are initialized
    // e.g. Wi-Fi reads AP configs from SD card
    LOG_I(TAG, "Publish event");
    system_event_emit(KERNEL_EVENT_BOOT_COMPLETED, nullptr, 0);

    return true;
}

}
