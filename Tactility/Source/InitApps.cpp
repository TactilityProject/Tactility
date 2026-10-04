#include <Tactility/TactilityPrivate.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <Tactility/MountPoints.h>
#include <Tactility/TactilityConfig.h>
#include <Tactility/file/File.h>
#include <Tactility/service/audio/Audio.h>

#include <app/install.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/start.h>

#include <tactility/device.h>
#include <tactility/drivers/camera.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/grove.h>
#include <tactility/drivers/led_strip.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/drivers/trackball.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/log.h>

#include <format>

namespace tt {

constexpr auto* TAG = "Tactility";

// region Default apps

// All apps below are converted to the new app-module + window-manager model, so their manifest
// is the new, global ::AppManifest, not this namespace's old tt::app::AppManifest.
namespace app {
    namespace addgps { extern const ::AppManifest manifest; }
    namespace alertdialog { extern const ::AppManifest manifest; }
    namespace apphub { extern const ::AppManifest manifest; }
    namespace apphubdetails { extern const ::AppManifest manifest; }
    namespace apppackagedetails { extern const ::AppManifest manifest; }
    namespace applist { extern const ::AppManifest manifest; }
    namespace apppackagelist { extern const ::AppManifest manifest; }
    namespace audiosettings { extern const ::AppManifest manifest; }
    namespace boot { extern const ::AppManifest manifest; }
    namespace development { extern const ::AppManifest manifest; }
    namespace display { extern const ::AppManifest manifest; }
    namespace files { extern const ::AppManifest manifest; }
    namespace fileselection { extern const ::AppManifest manifest; }
    namespace gpssettings { extern const ::AppManifest manifest; }
    namespace grovesettings { extern const ::AppManifest manifest; }
    namespace i2cscanner { extern const ::AppManifest manifest; }
    namespace imageviewer { extern const ::AppManifest manifest; }
    namespace inputdialog { extern const ::AppManifest manifest; }
    namespace launcher { extern const ::AppManifest manifest; }
    namespace ledstripsettings { extern const ::AppManifest manifest; }
    namespace localesettings { extern const ::AppManifest manifest; }
    namespace notes { extern const ::AppManifest manifest; }
    namespace power { extern const ::AppManifest manifest; }
    namespace poweroff { extern const ::AppManifest manifest; }
    namespace selectiondialog { extern const ::AppManifest manifest; }
    namespace settings { extern const ::AppManifest manifest; }
    namespace setup { extern const ::AppManifest manifest; }
    namespace shell { extern const ::AppManifest manifest; extern const ::AppManifest sh_manifest; }
    namespace systeminfo { extern const ::AppManifest manifest; }
    namespace terminal { extern const ::AppManifest manifest; }
    namespace timedatesettings { extern const ::AppManifest manifest; }
#ifdef CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED
    namespace touchcalibration { extern const ::AppManifest manifest; }
#endif
    namespace timezone { extern const ::AppManifest manifest; }
    namespace usbsettings { extern const ::AppManifest manifest; }
    namespace btmanage { extern const ::AppManifest manifest; }
    namespace btpeersettings { extern const ::AppManifest manifest; }
    namespace wifiapsettings { extern const ::AppManifest manifest; }
    namespace wificonnect { extern const ::AppManifest manifest; }
    namespace wifimanage { extern const ::AppManifest manifest; }

    namespace webserversettings { extern const ::AppManifest manifest; }
#ifdef ESP_PLATFORM
    namespace apwebserver { extern const ::AppManifest manifest; }
    namespace camera { extern const ::AppManifest manifest; }
    namespace crashdiagnostics { extern const ::AppManifest manifest; }
#if CONFIG_TT_TDECK_WORKAROUND == 1
    namespace keyboardsettings { extern const ::AppManifest manifest; } // T-Deck only for now
#endif
#endif

    namespace trackballsettings { extern const ::AppManifest manifest; } // T-Deck only for now

#if TT_FEATURE_SCREENSHOT_ENABLED
    namespace screenshot { extern const ::AppManifest manifest; }
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLED)
    namespace chat { extern const ::AppManifest manifest; }
#endif
}

// endregion

// List of all apps excluding Boot app (as Boot app calls this function indirectly)
static void registerInternalApps() {
    LOG_I(TAG, "Registering internal apps");

    app_manager_add(&app::alertdialog::manifest);
    app_manager_add(&app::apppackagedetails::manifest);
    app_manager_add(&app::apphub::manifest);
    app_manager_add(&app::apphubdetails::manifest);
    app_manager_add(&app::applist::manifest);
    app_manager_add(&app::apppackagelist::manifest);
    if (service::audio::isAvailable()) {
        app_manager_add(&app::audiosettings::manifest);
    }
    if (device_exists_of_type(&DISPLAY_TYPE)) {
        app_manager_add(&app::display::manifest);
    }
    if (device_exists_of_type(&LED_STRIP_TYPE)) {
        app_manager_add(&app::ledstripsettings::manifest);
    }
    app_manager_add(&app::files::manifest);
    app_manager_add(&app::fileselection::manifest);
    app_manager_add(&app::i2cscanner::manifest);
    app_manager_add(&app::imageviewer::manifest);
    app_manager_add(&app::inputdialog::manifest);
    app_manager_add(&app::launcher::manifest);
    app_manager_add(&app::localesettings::manifest);
    app_manager_add(&app::notes::manifest);
    if (device_exists_of_type(&POWER_SUPPLY_TYPE)) {
        app_manager_add(&app::poweroff::manifest);
    }
    app_manager_add(&app::settings::manifest);
    app_manager_add(&app::selectiondialog::manifest);
    app_manager_add(&app::setup::manifest);
    app_manager_add(&app::shell::manifest);
    app_manager_add(&app::shell::sh_manifest);
    app_manager_add(&app::systeminfo::manifest);
    app_manager_add(&app::timedatesettings::manifest);
    app_manager_add(&app::terminal::manifest);
#ifdef CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED
    app_manager_add(&app::touchcalibration::manifest);
#endif
    app_manager_add(&app::timezone::manifest);
    app_manager_add(&app::wifiapsettings::manifest);
    app_manager_add(&app::wificonnect::manifest);
    app_manager_add(&app::wifimanage::manifest);

    app_manager_add(&app::development::manifest);
    app_manager_add(&app::webserversettings::manifest);
#ifdef ESP_PLATFORM
    app_manager_add(&app::apwebserver::manifest);
    if (device_exists_of_type(&CAMERA_TYPE)) {
        app_manager_add(&app::camera::manifest);
    }
    app_manager_add(&app::crashdiagnostics::manifest);
#if defined(CONFIG_TT_TDECK_WORKAROUND)
        app_manager_add(&app::keyboardsettings::manifest);
#endif
#endif

    if (device_exists_of_type(&TRACKBALL_TYPE)) {
        app_manager_add(&app::trackballsettings::manifest);
    }

#if defined(CONFIG_TINYUSB_MSC_ENABLED) && CONFIG_TINYUSB_MSC_ENABLED
    app_manager_add(&app::usbsettings::manifest);
#endif

#if TT_FEATURE_SCREENSHOT_ENABLED
    app_manager_add(&app::screenshot::manifest);
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLED)
    app_manager_add(&app::chat::manifest);
#endif

    if (device_exists_of_type(&GROVE_TYPE)) {
        app_manager_add(&app::grovesettings::manifest);
    }

    if (device_exists_of_type(&UART_CONTROLLER_TYPE) || device_exists_of_type(&GROVE_TYPE)) {
        app_manager_add(&app::addgps::manifest);
        app_manager_add(&app::gpssettings::manifest);
    }

    if (device_exists_of_type(&POWER_SUPPLY_TYPE)) {
        app_manager_add(&app::power::manifest);
    }

#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED
    app_manager_add(&app::btmanage::manifest);
    app_manager_add(&app::btpeersettings::manifest);
#endif
}

// Registers every mounted filesystem's app install directory with app-module (see
// app_manager_install_path_add()/app_manager_install_path_scan() in app/install.h), then scans
// them once to register whatever's already installed there.
static void registerInstalledAppsFromFileSystems() {
    file_system_for_each(nullptr, [](auto* fs, void* context) {
        if (!file_system_is_mounted(fs)) return true;
        char path[128];
        if (file_system_get_path(fs, path, sizeof(path)) != ERROR_NONE) return true;
        const auto app_path = std::format("{}/tactility/app", path);
        if (!app_path.starts_with(file::MOUNT_POINT_SYSTEM) && file::isDirectory(app_path)) {
            LOG_I(TAG, "Registering install path %s", app_path.c_str());
            app_manager_install_path_add(app_path.c_str());
        }
        return true;
    });
    app_manager_install_path_scan();
}

void registerApps() {
    registerInternalApps();
    registerInstalledAppsFromFileSystems();
}

void startBootApp() {
    LOG_I(TAG, "Starting boot app");
    // The boot app takes care of registering system apps, user services and user apps, and starts LVGL.
    app_manager_add(&app::boot::manifest);
    uint32_t boot_instance_id = 0;
    AppStartContext boot_context = app_start_context_for_manifest(&app::boot::manifest);
    app_start_with_context(&boot_context, &boot_instance_id);
}

} // namespace
