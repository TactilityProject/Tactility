#include "Tactility/lvgl/Lvgl.h"

#include <Tactility/SystemEvents.h>
#include <Tactility/CpuAffinity.h>
#include <Tactility/Paths.h>
#include <Tactility/TactilityPrivate.h>
#include <Tactility/app/AppContext.h>
#include <Tactility/app/AppPaths.h>
#include <Tactility/app/alertdialog/AlertDialog.h>
#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/usb/Usb.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/settings/BootSettings.h>
#include <Tactility/settings/DisplaySettings.h>

#include <lvgl.h>
#include <tactility/log.h>

#include <atomic>

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

extern const AppManifest manifest;

static std::shared_ptr<hal::display::DisplayDevice> getHalDisplay() {
    return hal::findFirstDevice<hal::display::DisplayDevice>(hal::Device::Type::Display);
}

class BootApp : public App {

    // Snapshot of hal::usb::isUsbBootMode(), taken before the boot thread starts and
    // potentially clears the underlying flag via setupUsbBootMode()/resetUsbBootMode().
    // onShow() reads this instead of the live flag to avoid a race between the two.
    static std::atomic<bool> isUsbBootSplash;

    // Set by bootThreadCallback() when CONFIG_TT_USER_DATA_LOCATION_SD is defined but no SD card is mounted.
    // onShow() reads this to show an error instead of the normal splash, and boot halts instead of starting the launcher.
    static std::atomic<bool> sdCardMissing;

    Thread thread = Thread(
        "boot",
        5120,
        [] { return bootThreadCallback(); },
        getCpuAffinityConfiguration().system
    );

    static void setupDisplay() {
        const auto hal_display = getHalDisplay();
        if (hal_display == nullptr) {
            return;
        }

        settings::display::DisplaySettings settings;
        if (settings::display::load(settings)) {
            if (hal_display->getGammaCurveCount() > 0) {
                hal_display->setGammaCurve(settings.gammaCurve);
                LOG_I(TAG, "Gamma curve %d", settings.gammaCurve);
            }
        } else {
            settings = settings::display::getDefault();
        }

        if (hal_display->supportsBacklightDuty()) {
            LOG_I(TAG, "Backlight %d", settings.backlightDuty);
            hal_display->setBacklightDuty(settings.backlightDuty);
        } else {
            LOG_I(TAG, "No backlight");
        }
    }

    static bool setupUsbBootMode() {
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

    static void waitForMinimalSplashDuration(TickType_t startTime) {
        const auto end_time = kernel::getTicks();
        const auto ticks_passed = end_time - startTime;
        constexpr auto minimum_ticks = (CONFIG_TT_SPLASH_DURATION / portTICK_PERIOD_MS);
        if (minimum_ticks > ticks_passed) {
            kernel::delayTicks(minimum_ticks - ticks_passed);
        }
    }

    static int32_t bootThreadCallback() {
        LOG_I(TAG, "Starting boot thread");
        const auto start_time = kernel::getTicks();

        // Give the UI some time to redraw
        // If we don't do this, various init calls will read files and block SPI IO for the display
        // This would result in a blank/black screen being shown during this phase of the boot process
        // This works with 5 ms on a T-Lora Pager, so we give it 10 ms to be safe
        kernel::delayMillis(10);

        // TODO: Support for multiple displays
        LOG_I(TAG, "Setup display");
        setupDisplay(); // Set backlight
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
            waitForMinimalSplashDuration(start_time);
            // When SD card is missing, wait for dialog result
            if (!sdCardMissing) stop(manifest.appId);
            startNextApp();
        }

        // This event will likely block as other systems are initialized
        // e.g. Wi-Fi reads AP configs from SD card
        LOG_I(TAG, "Publish event");
        kernel::publishSystemEvent(kernel::SystemEvent::BootSplash);

        return 0;
    }

    static std::string getLauncherAppId() {
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
        if (findAppManifestById(boot_properties.launcherAppId) == nullptr) {
            LOG_E(TAG, "Launcher app %s not found", boot_properties.launcherAppId.c_str());
            return CONFIG_TT_LAUNCHER_APP_ID;
        }

        // The boot.properties launcher app id is valid
        return boot_properties.launcherAppId;
    }

    static void startNextApp() {
        if (sdCardMissing) {
            alertdialog::start("Error", "SD card not found.\nPlease insert one and reboot.", std::vector<const char*> { "Reboot" });
            return;
        }

#ifdef ESP_PLATFORM
        if (esp_reset_reason() == ESP_RST_PANIC) {
            crashdiagnostics::start();
            return;
        }
#endif
        auto launcher_app_id = getLauncherAppId();
        start(launcher_app_id);
    }

    static int getSmallestDimension() {
        auto* display = lv_display_get_default();
        int width = lv_display_get_horizontal_resolution(display);
        int height = lv_display_get_vertical_resolution(display);
        return std::min(width, height);
    }

public:

    void onCreate(AppContext& app) override {
        // Snapshot before the boot thread potentially clears the flag via setupUsbBootMode()
        isUsbBootSplash = hal::usb::isUsbBootMode();

        // Just in case this app is somehow resumed
        if (thread.getState() == Thread::State::Stopped) {
            thread.start();
        }
    }

    void onDestroy(AppContext& app) override {
        thread.join();
    }

    void onResult(AppContext& /*app*/, LaunchId /*launchId*/, Result /*result*/, std::unique_ptr<Bundle> /*bundle*/) override {
#ifdef ESP_PLATFORM
        esp_restart();
#endif
    }

    void onShow(AppContext& app, lv_obj_t* parent) override {
        lvgl::obj_set_style_bg_blacken(parent);
        lv_obj_set_style_border_width(parent, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_radius(parent, 0, LV_STATE_DEFAULT);

        auto* image = lv_image_create(parent);
        lv_obj_set_size(image, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);

        const auto paths = app.getPaths();
        const char* logo;
        // TODO: Replace with automatic asset buckets like on Android
        if (getSmallestDimension() < 150) { // e.g. Cardputer
            logo = isUsbBootSplash ? "logo_usb.png" : "logo_small.png";
        } else {
            logo = isUsbBootSplash ? "logo_usb.png" : "logo.png";
        }
        const auto logo_path = lvgl::PATH_PREFIX + paths->getAssetsPath(logo);
        LOG_I(TAG, "%s", logo_path.c_str());
        lv_image_set_src(image, logo_path.c_str());

#ifdef ESP_PLATFORM
        if (isUsbBootSplash) {
            auto* button = lv_button_create(parent);
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
};

std::atomic<bool> BootApp::isUsbBootSplash = false;
std::atomic<bool> BootApp::sdCardMissing = false;

extern const AppManifest manifest = {
    .appId = "Boot",
    .appName = "Boot",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::HideStatusBar | AppManifest::Flags::Hidden,
    .createApp = create<BootApp>
};

} // namespace
