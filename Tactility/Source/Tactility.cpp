#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#include <Tactility/InitEsp.h>
#include <app_esp32/module.h>
#endif

#include <format>
#include <memory>
#include <string>
#include <vector>

#include <app/event.h>
#include <app/install.h>
#include <app/manager.h>
#include <app/manifest.h>
#include <app/module.h>

#include <Tactility/Tactility.h>

#include <Tactility/CpuAffinity.h>
#include <Tactility/LogMessages.h>
#include <Tactility/MountPoints.h>
#include <Tactility/Paths.h>
#include <Tactility/TactilityConfig.h>
#include <Tactility/bluetooth/Bluetooth.h>
#include <Tactility/file/File.h>
#include <Tactility/hal/SdCard.h>
#include <Tactility/lvgl/Statusbar.h>
#include <Tactility/lvgl/TrackballInit.h>
#include <Tactility/lvgl/UsbHidInput.h>
#include <Tactility/network/NtpPrivate.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/service/audio/Audio.h>
#include <Tactility/settings/TimePrivate.h>
#include <Tactility/settings/TouchCalibrationSettings.h>

#include <crypt/module.h>

#include <gps/module.h>
#include <gps_generic/module.h>
#include <gps_meshtastic/module.h>

#include <crypt/module.h>
#include <lvgl/devices/pointer.h>
#include <lvgl/lvgl.h>
#include <lvgl/module.h>
#include <lvgl/widgets/toolbar.h>

#include <lvgl_window_manager/module.h>
#include <lvgl_window_manager/window_manager.h>

#include <tactility/concurrent/thread.h>
#include <tactility/device.h>
#include <tactility/drivers/audio_stream.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/grove.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/drivers/rtc.h>
#include <tactility/drivers/trackball.h>
#include <tactility/drivers/uart_controller.h>
#include <tactility/filesystem/file_mutex.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/kernel_init.h>
#include <tactility/log.h>
#include <tactility/memory.h>

namespace tt {

constexpr auto* TAG = "Tactility";

static DispatcherHandle_t mainDispatcherHandle = dispatcher_alloc();

void initFileMutexForLvgl();

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
    namespace audio { extern const ServiceManifest manifest; }
    namespace wifi { extern const ServiceManifest manifest; }
#ifdef ESP_PLATFORM
    namespace development { extern const ServiceManifest manifest; }
#endif
#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)
    namespace espnow { extern const ServiceManifest manifest; }
#endif
    // Secondary (UI)
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

// All apps below are converted to the new app-module + window-manager model, so their manifest
// is the new, global ::AppManifest, not this namespace's old tt::app::AppManifest.
namespace app {
    namespace addgps { extern const ::AppManifest manifest; }
    namespace alertdialog { extern const ::AppManifest manifest; }
    namespace apphub { extern const ::AppManifest manifest; }
    namespace apphubdetails { extern const ::AppManifest manifest; }
    namespace appdetails { extern const ::AppManifest manifest; }
    namespace applist { extern const ::AppManifest manifest; }
    namespace appsettings { extern const ::AppManifest manifest; }
    namespace audiosettings { extern const ::AppManifest manifest; }
    namespace boot { extern const ::AppManifest manifest; }
    namespace development { extern const ::AppManifest manifest; }
    namespace display { extern const ::AppManifest manifest; }
    namespace kerneldisplay { extern const ::AppManifest manifest; }
    namespace files { extern const ::AppManifest manifest; }
    namespace fileselection { extern const ::AppManifest manifest; }
    namespace gpssettings { extern const ::AppManifest manifest; }
    namespace grovesettings { extern const ::AppManifest manifest; }
    namespace i2cscanner { extern const ::AppManifest manifest; }
    namespace imageviewer { extern const ::AppManifest manifest; }
    namespace inputdialog { extern const ::AppManifest manifest; }
    namespace launcher { extern const ::AppManifest manifest; }
    namespace localesettings { extern const ::AppManifest manifest; }
    namespace notes { extern const ::AppManifest manifest; }
    namespace power { extern const ::AppManifest manifest; }
    namespace poweroff { extern const ::AppManifest manifest; }
    namespace selectiondialog { extern const ::AppManifest manifest; }
    namespace settings { extern const ::AppManifest manifest; }
    namespace setup { extern const ::AppManifest manifest; }
    namespace systeminfo { extern const ::AppManifest manifest; }
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

#ifdef ESP_PLATFORM
    namespace apwebserver { extern const ::AppManifest manifest; }
    namespace crashdiagnostics { extern const ::AppManifest manifest; }
    namespace webserversettings { extern const ::AppManifest manifest; }
#if CONFIG_TT_TDECK_WORKAROUND == 1
    namespace keyboardsettings { extern const ::AppManifest manifest; } // T-Deck only for now
#endif
#endif

    namespace trackballsettings { extern const ::AppManifest manifest; } // T-Deck only for now

#if TT_FEATURE_SCREENSHOT_ENABLED
    namespace screenshot { extern const ::AppManifest manifest; }
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)
    namespace chat { extern const ::AppManifest manifest; }
#endif
}

// endregion

// List of all apps excluding Boot app (as Boot app calls this function indirectly)
static void registerInternalApps() {
    LOG_I(TAG, "Registering internal apps");

    app_manager_add(&app::alertdialog::manifest);
    app_manager_add(&app::appdetails::manifest);
    app_manager_add(&app::apphub::manifest);
    app_manager_add(&app::apphubdetails::manifest);
    app_manager_add(&app::applist::manifest);
    app_manager_add(&app::appsettings::manifest);
    if (service::audio::isAvailable()) {
        app_manager_add(&app::audiosettings::manifest);
    }
    if (device_exists_of_type(&DISPLAY_TYPE)) {
        app_manager_add(&app::kerneldisplay::manifest);
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
    app_manager_add(&app::systeminfo::manifest);
    app_manager_add(&app::timedatesettings::manifest);
#ifdef CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED
    app_manager_add(&app::touchcalibration::manifest);
#endif
    app_manager_add(&app::timezone::manifest);
    app_manager_add(&app::wifiapsettings::manifest);
    app_manager_add(&app::wificonnect::manifest);
    app_manager_add(&app::wifimanage::manifest);

#ifdef ESP_PLATFORM
    app_manager_add(&app::apwebserver::manifest);
    app_manager_add(&app::webserversettings::manifest);
    app_manager_add(&app::crashdiagnostics::manifest);
    app_manager_add(&app::development::manifest);
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

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)
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

static void registerAndStartServices() {
    LOG_I(TAG, "Registering and starting primary system services");
    if (device_exists_of_type(&AUDIO_STREAM_TYPE)) {
        addService(service::audio::manifest);
    }
    addService(service::wifi::manifest);
#ifdef ESP_PLATFORM
    addService(service::development::manifest);
#endif

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)
    addService(service::espnow::manifest);
#endif
#ifdef ESP_PLATFORM
    addService(service::webserver::manifest);
#endif
#if defined(ESP_PLATFORM)
    if (device_exists_of_type(&RTC_TYPE)) {
        addService(service::rtctime::manifest);
    }
#endif
}

void createTempDirectory() {
    auto data_path = getUserDataPath();
    auto temp_path = std::format("{}/tmp", data_path);
    if (!file::isDirectory(temp_path)) {
        FileMutex mutex;
        file_mutex_get(&mutex, data_path.c_str());
        if (file_mutex_try_lock(&mutex, 1000 / portTICK_PERIOD_MS)) {
            if (!file::findOrCreateParentDirectory(temp_path, 0777)) {
                LOG_E(TAG, "Failed to create %s", data_path.c_str());
            } else if (mkdir(temp_path.c_str(), 0777) == 0) {
                LOG_I(TAG, "Created %s", temp_path.c_str());
            } else {
                LOG_E(TAG, "Failed to create %s", temp_path.c_str());
            }
            file_mutex_unlock(&mutex);
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

static void stopAppFromToolbar(lv_event_t*) {
    // Default nav action for any toolbar that doesn't override it itself. Prefer the topmost
    // new-model app if one is showing; fall back to the old system otherwise (this is what
    // every not-yet-converted app's toolbar still relies on).
    AppInstanceId topmost = 0;
    check(app_manager_get_topmost_instance_id(&topmost) == ERROR_NONE);
    // Async, non-blocking - must NOT call app_manager_stop() directly here: that
    // bound-waits (thread_join) for the app's own thread to finish, which needs the LVGL
    // lock to clean up - but this callback runs ON the LVGL task, which would deadlock
    // against itself.
    AppEvent event { .type = APP_EVENT_CLOSE, .timestamp = 0, .result = {} };
    app_event_emit(topmost, &event);
}

static lv_obj_t* windowManagerScreenInit(lv_obj_t* root) {
    lv_obj_t* vertical_container = lv_obj_create(root);
    lv_obj_set_size(vertical_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(vertical_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(vertical_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_gap(vertical_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(vertical_container, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(vertical_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(vertical_container, 0, LV_STATE_DEFAULT);

    lvgl::statusbar_create(vertical_container);

    auto* app_container = lv_obj_create(vertical_container);
    lv_obj_set_style_pad_all(app_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(app_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(app_container, LV_PCT(100));
    lv_obj_set_flex_grow(app_container, 1);
    lv_obj_set_flex_flow(app_container, LV_FLEX_FLOW_COLUMN);

    return app_container;
}

#ifdef CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED

// Applies the calibration persisted by the touch calibration app to the live pointer indev.
// lvgl_devices_attach() runs before onLvglStarted(), so the default indev already exists here.
static void applySavedTouchCalibration() {
    settings::touch::TouchCalibrationSettings settings = settings::touch::loadOrGetDefault();
    if (!settings.enabled || !settings::touch::isValid(settings)) {
        return;
    }

    LvglPointerCalibration calibration = {
        .x_min = settings.xMin,
        .x_max = settings.xMax,
        .y_min = settings.yMin,
        .y_max = settings.yMax,
    };

    lvgl_lock();
    auto* indev = lvgl_pointer_get_default();
    if (indev != nullptr) {
        lvgl_pointer_set_calibration(indev, &calibration);
    }
    lvgl_unlock();
}

#endif // CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED

static void onLvglStarted() {
    window_manager_configure(windowManagerScreenInit);
    check(module_ensure_started(&lvgl_window_manager_module) == ERROR_NONE);

    ToolbarConfig toolbar_config = { .nav_action_callback = stopAppFromToolbar };
    lvgl_toolbar_configure(&toolbar_config);

    addService(service::statusbar::manifest);
    addService(service::memorychecker::manifest);
#if defined(ESP_PLATFORM)
    addService(service::displayidle::manifest);
#endif
#if defined(CONFIG_TT_TDECK_WORKAROUND)
    addService(service::keyboardidle::manifest);
#endif
#if TT_FEATURE_SCREENSHOT_ENABLED
    addService(service::screenshot::manifest);
#endif

    lvgl::startUsbHidInput();
    lvgl::initTrackball();

#ifdef CONFIG_TT_TOUCH_CALIBRATION_SUPPORTED
    applySavedTouchCalibration();
#endif

    memory_print_stats();
}

static void onLvglStopped() {
    module_stop(&lvgl_window_manager_module);

    lvgl::stopUsbHidInput();

#if TT_FEATURE_SCREENSHOT_ENABLED
    check(service::removeService(service::screenshot::manifest.id));
#endif
#if defined(CONFIG_TT_TDECK_WORKAROUND)
    check(service::removeService(service::keyboardidle::manifest.id));
#endif
#if defined(ESP_PLATFORM)
    check(service::removeService(service::displayidle::manifest.id));
#endif
    check(service::removeService(service::memorychecker::manifest.id));
    check(service::removeService(service::statusbar::manifest.id));

    memory_print_stats();
}

void run(Module* const dtsModules[], const DtsDevice dtsDevices[]) {
    LOG_I(TAG, "Tactility v%s on %s (%s)", TT_VERSION, CONFIG_TT_DEVICE_NAME, CONFIG_TT_DEVICE_ID);

    LOG_I(TAG, "Initializing kernel");
    if (kernel_init(dtsModules, dtsDevices) != ERROR_NONE) {
        LOG_E(TAG, "Failed to initialize kernel");
        return;
    }

    check(module_ensure_started(&crypt_module) == ERROR_NONE);
    check(module_ensure_started(&gps_module) == ERROR_NONE);
    check(module_ensure_started(&gps_generic_module) == ERROR_NONE);
    check(module_ensure_started(&gps_meshtastic_module) == ERROR_NONE);
    // Registers the APP_LOCATION_MEMORY app loader (boot/launcher need it below).
    check(module_ensure_started(&app_module) == ERROR_NONE);
#ifdef ESP_PLATFORM
    check(module_ensure_started(&app_esp32_module) == ERROR_NONE);
#endif

#ifdef ESP_PLATFORM
    initEsp();
#endif

    settings::initTimeZone();

    // Attempt to start all disabled SD cards (some require delayed init)
    hal::sdcard::startAll();

    network::ntp::init();
    bluetooth::systemStart();

    registerAndStartServices();

    // Must start right before LVGL
    initFileMutexForLvgl();

    lvgl_module_configure((LvglModuleConfig) {
        .on_start = onLvglStarted,
        .on_stop = onLvglStopped,
        .task_priority = THREAD_PRIORITY_HIGHER,
        /** Minimum seems to be about 3500. In some scenarios, the WiFi app crashes at 8192,
         * so we now have 9120 to run in a stable manner. We should figure out a way to avoid this.
         * Perhaps we can give apps their own stack space and deal with lvgl callback handlers in a clever way. */
        .task_stack_size = 9120,
#ifdef ESP_PLATFORM
        .task_affinity = getCpuAffinityConfiguration().graphics
#endif
    });
    check(module_ensure_started(&lvgl_module) == ERROR_NONE);

    LOG_I(TAG, "Core systems ready");

    LOG_I(TAG, "Starting boot app");
    // The boot app takes care of registering system apps, user services and user apps.
    // It's a new-model (app-module + window-manager) app now, replacing the old app::start().
    app_manager_add(&app::boot::manifest);
    uint32_t boot_instance_id = 0;
    app_manager_start(app::boot::manifest.id, &boot_instance_id);

    LOG_I(TAG, "Main dispatcher ready");
    while (true) {
        dispatcher_consume(mainDispatcherHandle);
    }
}

MainDispatcher getMainDispatcher() {
    return MainDispatcher(mainDispatcherHandle);
}

} // namespace
