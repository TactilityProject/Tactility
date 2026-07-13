#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <format>

#include <Tactility/Tactility.h>
#include <Tactility/TactilityConfig.h>

#include <Tactility/LogMessages.h>
#include <Tactility/CpuAffinity.h>
#include <Tactility/MountPoints.h>
#include <Tactility/app/AppManifestParsing.h>
#include <Tactility/app/AppRegistration.h>
#include <Tactility/file/File.h>
#include <Tactility/file/FileLock.h>
#include <Tactility/hal/HalPrivate.h>
#include <Tactility/lvgl/LvglPrivate.h>
#include <Tactility/network/NtpPrivate.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/settings/TimePrivate.h>

#include <tactility/concurrent/thread.h>
#include <tactility/crypt_module.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/grove.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/drivers/rtc.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/hal_device_module.h>
#include <tactility/kernel_init.h>
#include <tactility/log.h>
#include <tactility/lvgl_module.h>

#ifdef ESP_PLATFORM
#include <Tactility/InitEsp.h>
#endif

#include "Tactility/Paths.h"


#include <Tactility/bluetooth/Bluetooth.h>

namespace tt {

constexpr auto* TAG = "Tactility";

static const Configuration* config_instance = nullptr;
static DispatcherHandle_t mainDispatcherHandle = dispatcher_alloc();

namespace {

void mainDispatcherTrampoline(void* context) {
    auto* function = static_cast<MainDispatcher::Function*>(context);
    (*function)();
    delete function;
}

} // namespace

bool MainDispatcher::dispatch(Function function, TickType_t timeout) const {
    auto* boxed = new Function(std::move(function));
    if (dispatcher_dispatch_timed(handle, boxed, mainDispatcherTrampoline, timeout) != ERROR_NONE) {
        delete boxed;
        return false;
    }
    return true;
}

// region Default services
namespace service {
    // Primary
    namespace gps { extern const ServiceManifest manifest; }
    namespace wifi { extern const ServiceManifest manifest; }
#ifdef ESP_PLATFORM
    namespace development { extern const ServiceManifest manifest; }
#endif
#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)
    namespace espnow { extern const ServiceManifest manifest; }
#endif
    // Secondary (UI)
    namespace gui { extern const ServiceManifest manifest; }
    namespace loader { extern const ServiceManifest manifest; }
    namespace memorychecker { extern const ServiceManifest manifest; }
    namespace statusbar { extern const ServiceManifest manifest; }
#ifdef ESP_PLATFORM
    namespace displayidle { extern const ServiceManifest manifest; }
    namespace keyboardidle { extern const ServiceManifest manifest; }
    namespace rtctime { extern const ServiceManifest manifest; }
#endif
#if TT_FEATURE_SCREENSHOT_ENABLED
    namespace screenshot { extern const ServiceManifest manifest; }
#endif
#ifdef ESP_PLATFORM
    namespace webserver { extern const ServiceManifest manifest; }
#endif

}

// endregion

// region Default apps

namespace app {
    namespace addgps { extern const AppManifest manifest; }
    namespace alertdialog { extern const AppManifest manifest; }
    namespace apphub { extern const AppManifest manifest; }
    namespace apphubdetails { extern const AppManifest manifest; }
    namespace appdetails { extern const AppManifest manifest; }
    namespace applist { extern const AppManifest manifest; }
    namespace appsettings { extern const AppManifest manifest; }
    namespace boot { extern const AppManifest manifest; }
    namespace development { extern const AppManifest manifest; }
    namespace display { extern const AppManifest manifest; }
    namespace kerneldisplay { extern const AppManifest manifest; }
    namespace files { extern const AppManifest manifest; }
    namespace fileselection { extern const AppManifest manifest; }
    namespace gpssettings { extern const AppManifest manifest; }
    namespace grovesettings { extern const AppManifest manifest; }
    namespace i2cscanner { extern const AppManifest manifest; }
    namespace imageviewer { extern const AppManifest manifest; }
    namespace inputdialog { extern const AppManifest manifest; }
    namespace launcher { extern const AppManifest manifest; }
    namespace localesettings { extern const AppManifest manifest; }
    namespace notes { extern const AppManifest manifest; }
    namespace power { extern const AppManifest manifest; }
    namespace poweroff { extern const AppManifest manifest; }
    namespace selectiondialog { extern const AppManifest manifest; }
    namespace settings { extern const AppManifest manifest; }
    namespace setup { extern const AppManifest manifest; }
    namespace systeminfo { extern const AppManifest manifest; }
    namespace timedatesettings { extern const AppManifest manifest; }
#ifdef CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED
    namespace touchcalibration { extern const AppManifest manifest; }
#endif
    namespace timezone { extern const AppManifest manifest; }
    namespace usbsettings { extern const AppManifest manifest; }
    namespace btmanage { extern const AppManifest manifest; }
    namespace btpeersettings { extern const AppManifest manifest; }
    namespace wifiapsettings { extern const AppManifest manifest; }
    namespace wificonnect { extern const AppManifest manifest; }
    namespace wifimanage { extern const AppManifest manifest; }

#ifdef ESP_PLATFORM
    namespace apwebserver { extern const AppManifest manifest; }
    namespace crashdiagnostics { extern const AppManifest manifest; }
    namespace webserversettings { extern const AppManifest manifest; }
#if CONFIG_TT_TDECK_WORKAROUND == 1
    namespace keyboardsettings { extern const AppManifest manifest; } // T-Deck only for now
    namespace trackballsettings { extern const AppManifest manifest; } // T-Deck only for now
#endif
#endif

#if TT_FEATURE_SCREENSHOT_ENABLED
    namespace screenshot { extern const AppManifest manifest; }
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)
    namespace chat { extern const AppManifest manifest; }
#endif
}

// endregion

// List of all apps excluding Boot app (as Boot app calls this function indirectly)
static void registerInternalApps() {
    LOG_I(TAG, "Registering internal apps");

    addAppManifest(app::alertdialog::manifest);
    addAppManifest(app::appdetails::manifest);
    addAppManifest(app::apphub::manifest);
    addAppManifest(app::apphubdetails::manifest);
    addAppManifest(app::applist::manifest);
    addAppManifest(app::appsettings::manifest);
    if (device_exists_of_type(&DISPLAY_TYPE)) {
        addAppManifest(app::kerneldisplay::manifest);
    } else if (hal::hasDevice(hal::Device::Type::Display)) {
        addAppManifest(app::display::manifest);
    }
    addAppManifest(app::files::manifest);
    addAppManifest(app::fileselection::manifest);
    addAppManifest(app::i2cscanner::manifest);
    addAppManifest(app::imageviewer::manifest);
    addAppManifest(app::inputdialog::manifest);
    addAppManifest(app::launcher::manifest);
    addAppManifest(app::localesettings::manifest);
    addAppManifest(app::notes::manifest);
    if (device_exists_of_type(&POWER_SUPPLY_TYPE)) {
        addAppManifest(app::poweroff::manifest);
    }
    addAppManifest(app::settings::manifest);
    addAppManifest(app::selectiondialog::manifest);
    addAppManifest(app::setup::manifest);
    addAppManifest(app::systeminfo::manifest);
    addAppManifest(app::timedatesettings::manifest);
#ifdef CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED
    addAppManifest(app::touchcalibration::manifest);
#endif
    addAppManifest(app::timezone::manifest);
    addAppManifest(app::wifiapsettings::manifest);
    addAppManifest(app::wificonnect::manifest);
    addAppManifest(app::wifimanage::manifest);

#ifdef ESP_PLATFORM
    addAppManifest(app::apwebserver::manifest);
    addAppManifest(app::webserversettings::manifest);
    addAppManifest(app::crashdiagnostics::manifest);
    addAppManifest(app::development::manifest);
#if defined(CONFIG_TT_TDECK_WORKAROUND)
        addAppManifest(app::keyboardsettings::manifest);
        addAppManifest(app::trackballsettings::manifest);
#endif
#endif

#if defined(CONFIG_TINYUSB_MSC_ENABLED) && CONFIG_TINYUSB_MSC_ENABLED
    addAppManifest(app::usbsettings::manifest);
#endif

#if TT_FEATURE_SCREENSHOT_ENABLED
    addAppManifest(app::screenshot::manifest);
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)
    addAppManifest(app::chat::manifest);
#endif

    if (device_exists_of_type(&GROVE_TYPE)) {
        addAppManifest(app::grovesettings::manifest);
    }

    if (device_exists_of_type(&UART_CONTROLLER_TYPE) || device_exists_of_type(&GROVE_TYPE)) {
        addAppManifest(app::addgps::manifest);
        addAppManifest(app::gpssettings::manifest);
    }

    if (device_exists_of_type(&POWER_SUPPLY_TYPE)) {
        addAppManifest(app::power::manifest);
    }

#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED
    addAppManifest(app::btmanage::manifest);
    addAppManifest(app::btpeersettings::manifest);
#endif
}

static void registerInstalledApp(std::string path) {
    LOG_I(TAG, "Registering app at %s", path.c_str());
    std::string manifest_path = path + "/manifest.properties";
    if (!file::isFile(manifest_path)) {
        LOG_E(TAG, "Manifest not found at %s", manifest_path.c_str());
        return;
    }

    app::AppManifest manifest;
    if (!app::parseManifest(manifest_path, manifest)) {
        LOG_E(TAG, "Failed to parse manifest at %s", manifest_path.c_str());
        return;
    }

    manifest.appCategory = app::Category::User;
    manifest.appLocation = app::Location::external(path);

    app::addAppManifest(manifest);
}

static void registerInstalledApps(const std::string& path) {
    LOG_I(TAG, "Registering apps from %s", path.c_str());

    file::listDirectory(path, [&path](const auto& entry) {
        auto absolute_path = std::format("{}/{}", path, entry.d_name);
        if (file::isDirectory(absolute_path)) {
            registerInstalledApp(absolute_path);
        }
    });
}

static void registerInstalledAppsFromFileSystems() {
    file_system_for_each(nullptr, [](auto* fs, void* context) {
        if (!file_system_is_mounted(fs)) return true;
        char path[128];
        if (file_system_get_path(fs, path, sizeof(path)) != ERROR_NONE) return true;
        const auto app_path = std::format("{}/tactility/app", path);
        if (!app_path.starts_with(file::MOUNT_POINT_SYSTEM) && file::isDirectory(app_path)) {
            LOG_I(TAG, "Registering apps from %s", app_path.c_str());
            registerInstalledApps(app_path);
        }
        return true;
    });
}

static void registerAndStartSecondaryServices() {
    LOG_I(TAG, "Registering and starting secondary system services");
    addService(service::loader::manifest);
    addService(service::gui::manifest);
    addService(service::statusbar::manifest);
    addService(service::memorychecker::manifest);
#if defined(ESP_PLATFORM)
    if (device_exists_of_type(&RTC_TYPE)) {
        addService(service::rtctime::manifest);
    }
    addService(service::displayidle::manifest);
#if defined(CONFIG_TT_TDECK_WORKAROUND)
    addService(service::keyboardidle::manifest);
#endif
#endif
#if TT_FEATURE_SCREENSHOT_ENABLED
    addService(service::screenshot::manifest);
#endif
}

static void registerAndStartPrimaryServices() {
    LOG_I(TAG, "Registering and starting primary system services");
    addService(service::gps::manifest);
    addService(service::wifi::manifest);
#ifdef ESP_PLATFORM
    addService(service::development::manifest);
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) && !defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)
    addService(service::espnow::manifest);
#endif
#ifdef ESP_PLATFORM
    addService(service::webserver::manifest);
#endif
}

void createTempDirectory() {
    auto data_path = getUserDataPath();
    auto temp_path = std::format("{}/tmp", data_path);
    if (!file::isDirectory(temp_path)) {
        auto lock = file::getLock(data_path)->asScopedLock();
        if (lock.lock(1000 / portTICK_PERIOD_MS)) {
            if (!file::findOrCreateParentDirectory(temp_path, 0777)) {
                LOG_E(TAG, "Failed to create %s", data_path.c_str());
            } else if (mkdir(temp_path.c_str(), 0777) == 0) {
                LOG_I(TAG, "Created %s", temp_path.c_str());
            } else {
                LOG_E(TAG, "Failed to create %s", temp_path.c_str());
            }
        } else {
            LOG_E(TAG, LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, data_path.c_str());
        }
    } else {
        LOG_I(TAG, "Found existing %s", temp_path.c_str());
    }
}

void prepareFileSystems() {
    createTempDirectory();
}

void registerApps() {
    registerInternalApps();
    registerInstalledAppsFromFileSystems();
}

void run(const Configuration& config, Module* dtsModules[], DtsDevice dtsDevices[]) {
    LOG_I(TAG, "Tactility v%s on %s (%s)", TT_VERSION, CONFIG_TT_DEVICE_NAME, CONFIG_TT_DEVICE_ID);

    assert(config.hardware);

    LOG_I(TAG, "Initializing kernel");
    if (kernel_init(dtsModules, dtsDevices) != ERROR_NONE) {
        LOG_E(TAG, "Failed to initialize kernel");
        return;
    }

    // hal-device-module
    check(module_construct_add_start(&hal_device_module) == ERROR_NONE);

    // crypt-module
    check(module_construct_add_start(&crypt_module) == ERROR_NONE);

    // Assign early so starting services can use it
    config_instance = &config;

#ifdef ESP_PLATFORM
    initEsp();
#endif
    file::setFindLockFunction(file::findLock);
    settings::initTimeZone();
    hal::init(*config.hardware);
    network::ntp::init();
    bluetooth::systemStart();

    registerAndStartPrimaryServices();

    lvgl_module_configure((LvglModuleConfig) {
        .on_start = lvgl::attachDevices,
        .on_stop = lvgl::detachDevices,
        .task_priority = THREAD_PRIORITY_HIGHER,
        /** Minimum seems to be about 3500. In some scenarios, the WiFi app crashes at 8192,
         * so we now have 9120 to run in a stable manner. We should figure out a way to avoid this.
         * Perhaps we can give apps their own stack space and deal with lvgl callback handlers in a clever way. */
        .task_stack_size = 9120,
#ifdef ESP_PLATFORM
        .task_affinity = getCpuAffinityConfiguration().graphics
#endif
    });
    check(module_construct(&lvgl_module) == ERROR_NONE);
    check(module_add(&lvgl_module) == ERROR_NONE);
    lvgl::start();

    registerAndStartSecondaryServices();

    LOG_I(TAG, "Core systems ready");

    LOG_I(TAG, "Starting boot app");
    // The boot app takes care of registering system apps, user services and user apps
    addAppManifest(app::boot::manifest);
    app::start(app::boot::manifest.appId);

    LOG_I(TAG, "Main dispatcher ready");
    while (true) {
        dispatcher_consume(mainDispatcherHandle);
    }
}

/** return the configuration or nullptr if it's not initialized */
const Configuration* getConfiguration() {
    return config_instance;
}

MainDispatcher getMainDispatcher() {
    return MainDispatcher(mainDispatcherHandle);
}

} // namespace
