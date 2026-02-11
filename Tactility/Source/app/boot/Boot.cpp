#include "Tactility/lvgl/Lvgl.h"

#include <Tactility/TactilityCore.h>
#include <Tactility/TactilityPrivate.h>
#include <Tactility/app/AppContext.h>
#include <Tactility/app/AppPaths.h>
#include <Tactility/CpuAffinity.h>
#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/usb/Usb.h>
#include <Tactility/kernel/SystemEvents.h>
#include <Tactility/Logger.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/service/loader/Loader.h>
#include <Tactility/settings/BootSettings.h>
#include <Tactility/settings/DisplaySettings.h>

#include <lvgl.h>

#ifdef ESP_PLATFORM
#include "Tactility/app/crashdiagnostics/CrashDiagnostics.h"
#include <Tactility/kernel/PanicHandler.h>
#include <sdkconfig.h>
#else
#define CONFIG_TT_SPLASH_DURATION 0
#endif

namespace tt::app::boot {

static const auto LOGGER = Logger("Boot");

extern const AppManifest manifest;

static std::shared_ptr<hal::display::DisplayDevice> getHalDisplay() {
    return hal::findFirstDevice<hal::display::DisplayDevice>(hal::Device::Type::Display);
}

class BootApp : public App {

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
                LOGGER.info("Gamma curve {}", settings.gammaCurve);
            }
        } else {
            settings = settings::display::getDefault();
        }

        if (hal_display->supportsBacklightDuty()) {
            LOGGER.info("Backlight {}", settings.backlightDuty);
            hal_display->setBacklightDuty(settings.backlightDuty);
        } else {
            LOGGER.info("No backlight");
        }
    }

    static bool setupUsbBootMode() {
        if (!hal::usb::isUsbBootMode()) {
            return false;
        }

        LOGGER.info("Rebooting into mass storage device mode");
        auto mode = hal::usb::getUsbBootMode();  // Get mode before reset
        hal::usb::resetUsbBootMode();
        if (mode == hal::usb::BootMode::Flash) {
            if (!hal::usb::startMassStorageWithFlash()) {
                LOGGER.error("Unable to start flash mass storage");
                return false;
            }
        } else if (mode == hal::usb::BootMode::Sdmmc) {
            if (!hal::usb::startMassStorageWithSdmmc()) {
                LOGGER.error("Unable to start SD mass storage");
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
        LOGGER.info("Starting boot thread");
        const auto start_time = kernel::getTicks();

        // Give the UI some time to redraw
        // If we don't do this, various init calls will read files and block SPI IO for the display
        // This would result in a blank/black screen being shown during this phase of the boot process
        // This works with 5 ms on a T-Lora Pager, so we give it 10 ms to be safe
        kernel::delayMillis(10);

        // TODO: Support for multiple displays
        LOGGER.info("Setup display");
        setupDisplay(); // Set backlight
        prepareFileSystems();

        if (!setupUsbBootMode()) {
            LOGGER.info("initFromBootApp");
            registerApps();
            waitForMinimalSplashDuration(start_time);
            stop(manifest.appId);
            startNextApp();
        }

        // This event will likely block as other systems are initialized
        // e.g. Wi-Fi reads AP configs from SD card
        LOGGER.info("Publish event");
        kernel::publishSystemEvent(kernel::SystemEvent::BootSplash);

        return 0;
    }

    static std::string getLauncherAppId() {
        settings::BootSettings boot_properties;
        std::string launcher_app_id;
        // When boot.properties hasn't been overridden, return default
        if (!settings::loadBootSettings(boot_properties)) {
            return CONFIG_TT_LAUNCHER_APP_ID;
        }

        // When boot properties didn't specify an override, return default
        if (boot_properties.launcherAppId.empty()) {
            LOGGER.error("Failed to load launcher configuration, or launcher not configured");
            return CONFIG_TT_LAUNCHER_APP_ID;
        }

        // If the app in the boot.properties does not exist, return default
        if (findAppManifestById(launcher_app_id) == nullptr) {
            LOGGER.error("Launcher app {} not found", launcher_app_id);
            return CONFIG_TT_LAUNCHER_APP_ID;
        }

        // The boot.properties launcher app id is valid
        return boot_properties.launcherAppId;
    }

    static void startNextApp() {
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
        // Just in case this app is somehow resumed
        if (thread.getState() == Thread::State::Stopped) {
            thread.start();
        }
    }

    void onDestroy(AppContext& app) override {
        thread.join();
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
            logo = hal::usb::isUsbBootMode() ? "logo_usb.png" : "logo_small.png";
        } else {
            logo = hal::usb::isUsbBootMode() ? "logo_usb.png" : "logo.png";
        }
        const auto logo_path = lvgl::PATH_PREFIX + paths->getAssetsPath(logo);
        LOGGER.info("{}", logo_path);
        lv_image_set_src(image, logo_path.c_str());
    }
};

extern const AppManifest manifest = {
    .appId = "Boot",
    .appName = "Boot",
    .appCategory = Category::System,
    .appFlags = AppManifest::Flags::HideStatusBar | AppManifest::Flags::Hidden,
    .createApp = create<BootApp>
};

} // namespace
