#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <format>
#include <map>

#include <Tactility/Tactility.h>
#include <Tactility/TactilityConfig.h>

#include <Tactility/app/AppManifestParsing.h>
#include <Tactility/app/AppRegistration.h>
#include <Tactility/CpuAffinity.h>
#include <Tactility/DispatcherThread.h>
#include <Tactility/file/File.h>
#include <Tactility/file/FileLock.h>
#include <Tactility/file/PropertiesFile.h>
#include <Tactility/hal/HalPrivate.h>
#include <Tactility/hal/sdcard/SdCardDevice.h>
#include <Tactility/Logger.h>
#include <Tactility/LogMessages.h>
#include <Tactility/lvgl/LvglPrivate.h>
#include <Tactility/MountPoints.h>
#include <Tactility/network/NtpPrivate.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/settings/TimePrivate.h>

#include <tactility/concurrent/thread.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/hal_device_module.h>
#include <tactility/kernel_init.h>
#include <tactility/lvgl_module.h>

#ifdef ESP_PLATFORM
#include <tactility/drivers/root.h>
#include <Tactility/InitEsp.h>
#endif

namespace tt {

static auto LOGGER = Logger("Tactility");

static const Configuration* config_instance = nullptr;
static Dispatcher mainDispatcher;

// region Default services
namespace service {
    // Primary
    namespace gps { extern const ServiceManifest manifest; }
    namespace wifi { extern const ServiceManifest manifest; }
    namespace sdcard { extern const ServiceManifest manifest; }
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
    namespace files { extern const AppManifest manifest; }
    namespace fileselection { extern const AppManifest manifest; }
    namespace gpssettings { extern const AppManifest manifest; }
    namespace i2cscanner { extern const AppManifest manifest; }
    namespace imageviewer { extern const AppManifest manifest; }
    namespace inputdialog { extern const AppManifest manifest; }
    namespace launcher { extern const AppManifest manifest; }
    namespace localesettings { extern const AppManifest manifest; }
    namespace notes { extern const AppManifest manifest; }
    namespace power { extern const AppManifest manifest; }
    namespace selectiondialog { extern const AppManifest manifest; }
    namespace settings { extern const AppManifest manifest; }
    namespace systeminfo { extern const AppManifest manifest; }
    namespace timedatesettings { extern const AppManifest manifest; }
    namespace timezone { extern const AppManifest manifest; }
    namespace usbsettings { extern const AppManifest manifest; }
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
    LOGGER.info("Registering internal apps");

    addAppManifest(app::alertdialog::manifest);
    addAppManifest(app::appdetails::manifest);
    addAppManifest(app::apphub::manifest);
    addAppManifest(app::apphubdetails::manifest);
    addAppManifest(app::applist::manifest);
    addAppManifest(app::appsettings::manifest);
    addAppManifest(app::display::manifest);
    addAppManifest(app::files::manifest);
    addAppManifest(app::fileselection::manifest);
    addAppManifest(app::i2cscanner::manifest);
    addAppManifest(app::imageviewer::manifest);
    addAppManifest(app::inputdialog::manifest);
    addAppManifest(app::launcher::manifest);
    addAppManifest(app::localesettings::manifest);
    addAppManifest(app::notes::manifest);
    addAppManifest(app::settings::manifest);
    addAppManifest(app::selectiondialog::manifest);
    addAppManifest(app::systeminfo::manifest);
    addAppManifest(app::timedatesettings::manifest);
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

    if (device_exists_of_type(&UART_CONTROLLER_TYPE)) {
        addAppManifest(app::addgps::manifest);
        addAppManifest(app::gpssettings::manifest);
    }

    if (hal::hasDevice(hal::Device::Type::Power)) {
        addAppManifest(app::power::manifest);
    }
}

static void registerInstalledApp(std::string path) {
    LOGGER.info("Registering app at {}", path);
    std::string manifest_path = path + "/manifest.properties";
    if (!file::isFile(manifest_path)) {
        LOGGER.error("Manifest not found at {}", manifest_path);
        return;
    }

    std::map<std::string, std::string> properties;
    if (!file::loadPropertiesFile(manifest_path, properties)) {
        LOGGER.error("Failed to load manifest at {}", manifest_path);
        return;
    }

    app::AppManifest manifest;
    if (!app::parseManifest(properties, manifest)) {
        LOGGER.error("Failed to parse manifest at {}", manifest_path);
        return;
    }

    manifest.appCategory = app::Category::User;
    manifest.appLocation = app::Location::external(path);

    app::addAppManifest(manifest);
}

static void registerInstalledApps(const std::string& path) {
    LOGGER.info("Registering apps from {}", path);

    file::listDirectory(path, [&path](const auto& entry) {
        auto absolute_path = std::format("{}/{}", path, entry.d_name);
        if (file::isDirectory(absolute_path)) {
            registerInstalledApp(absolute_path);
        }
    });
}

static void registerInstalledAppsFromSdCard(const std::shared_ptr<hal::sdcard::SdCardDevice>& sdcard) {
    auto sdcard_root_path = sdcard->getMountPath();
    auto app_path = std::format("{}/app", sdcard_root_path);
    if (file::isDirectory(app_path)) {
        registerInstalledApps(app_path);
    }
}

static void registerInstalledAppsFromSdCards() {
    auto sdcard_devices = hal::findDevices<hal::sdcard::SdCardDevice>(hal::Device::Type::SdCard);
    for (const auto& sdcard : sdcard_devices) {
        if (sdcard->isMounted()) {
            LOGGER.info("Registering apps from {}", sdcard->getMountPath());
            registerInstalledAppsFromSdCard(sdcard);
        }
    }
}

static void registerAndStartSecondaryServices() {
    LOGGER.info("Registering and starting secondary system services");
    addService(service::loader::manifest);
    addService(service::gui::manifest);
    addService(service::statusbar::manifest);
    addService(service::memorychecker::manifest);
#if defined(ESP_PLATFORM)
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
    LOGGER.info("Registering and starting primary system services");
    addService(service::gps::manifest);
    if (hal::hasDevice(hal::Device::Type::SdCard)) {
        addService(service::sdcard::manifest);
    }
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

void createTempDirectory(const std::string& rootPath) {
    auto temp_path = std::format("{}/tmp", rootPath);
    if (!file::isDirectory(temp_path)) {
        auto lock = file::getLock(rootPath)->asScopedLock();
        if (lock.lock(1000 / portTICK_PERIOD_MS)) {
            if (mkdir(temp_path.c_str(), 0777) == 0) {
                LOGGER.info("Created {}", temp_path);
            } else {
                LOGGER.error("Failed to create {}", temp_path);
            }
        } else {
            LOGGER.error(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, rootPath);
        }
    } else {
        LOGGER.info("Found existing {}", temp_path);
    }
}

void prepareFileSystems() {
    // Temporary directories for SD cards
    auto sdcard_devices = hal::findDevices<hal::sdcard::SdCardDevice>(hal::Device::Type::SdCard);
    for (const auto& sdcard : sdcard_devices) {
        if (sdcard->isMounted()) {
            createTempDirectory(sdcard->getMountPath());
        }
    }
    // Temporary directory for /data
    if (file::isDirectory(file::MOUNT_POINT_DATA)) {
        createTempDirectory(file::MOUNT_POINT_DATA);
    }
}

void registerApps() {
    registerInternalApps();
    auto data_apps_path = std::format("{}/app", file::MOUNT_POINT_DATA);
    if (file::isDirectory(data_apps_path)) {
        registerInstalledApps(data_apps_path);
    }
    registerInstalledAppsFromSdCards();
}

void run(const Configuration& config, Module* deviceModule, Module* dtsModules[], DtsDevice dtsDevices[]) {
    LOGGER.info("Tactility v{} on {} ({})", TT_VERSION, CONFIG_TT_DEVICE_NAME, CONFIG_TT_DEVICE_ID);

    assert(config.hardware);

    LOGGER.info(R"(Calling kernel_init with modules "{}")", deviceModule->name);
    if (kernel_init(deviceModule, dtsModules, dtsDevices) != ERROR_NONE) {
        LOGGER.error("Failed to initialize kernel");
        return;
    }

    // hal-device-module
    check(module_construct(&hal_device_module) == ERROR_NONE);
    check(module_add(&hal_device_module) == ERROR_NONE);
    check(module_start(&hal_device_module) == ERROR_NONE);

    const hal::Configuration& hardware = *config.hardware;

    // Assign early so starting services can use it
    config_instance = &config;

#ifdef ESP_PLATFORM
    initEsp();
#endif
    file::setFindLockFunction(file::findLock);
    settings::initTimeZone();
    hal::init(*config.hardware);
    network::ntp::init();

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

    LOGGER.info("Core systems ready");

    LOGGER.info("Starting boot app");
    // The boot app takes care of registering system apps, user services and user apps
    addAppManifest(app::boot::manifest);
    app::start(app::boot::manifest.appId);

    LOGGER.info("Main dispatcher ready");
    while (true) {
        mainDispatcher.consume();
    }
}

/** return the configuration or nullptr if it's not initialized */
const Configuration* getConfiguration() {
    return config_instance;
}

Dispatcher& getMainDispatcher() {
    return mainDispatcher;
}

} // namespace
