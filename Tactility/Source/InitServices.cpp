#include <Tactility/TactilityPrivate.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/settings/RadioSettings.h>

#include <generated_assets.h>
#include <location/module.h>
#include <app_hub/module.h>
#include <development/module.h>
#include <development/remote_access.h>
#include <bluetooth/module.h>
#include <wifi/module.h>
#include <mesh/module.h>
#include <mesh_meshcore/module.h>
#include <mesh_meshtastic/module.h>

#if defined(__linux__) || defined(__APPLE__)
#include <tactility/posix_services.h>
#endif

#include <lvgl/devices/display.h>
#include <lvgl/lvgl.h>
#include <atomic>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/audio_stream.h>
#include <tactility/drivers/rtc.h>
#include <gps/gps.h>
#include <tactility/module.h>

namespace tt {

static void redrawScreen() {
    if (lvgl_is_running() && lvgl_try_lock(pdMS_TO_TICKS(1000))) {
        lv_obj_invalidate(lv_screen_active());
        lvgl_unlock();
    }
}

// The viewer shows the UI upright with this: LVGL maps positions of the touch device, which has the native orientation
// of the display, to the UI by rotating them. A display that rotates in software hands its driver pixels that are
// rotated in the same way, while one that rotates in hardware receives the UI as it is
static uint8_t remoteOrientation() {
    // The UI lock can be busy for a while when the UI is rebuilt, and LVGL is gone while it restarts.
    // The last orientation that was read stays valid then, as reporting 0 would turn the viewer for a moment
    static std::atomic<uint8_t> lastOrientation { 0 };
    if (lvgl_is_running() && lvgl_try_lock(pdMS_TO_TICKS(250))) {
        if (auto* display = lv_display_get_default(); display != nullptr) {
            uint8_t orientation = static_cast<uint8_t>(lv_display_get_rotation(display)) & DEVELOPMENT_REMOTE_ORIENTATION_ROTATION_MASK;
            if (lvgl_display_is_software_rotated(display)) {
                orientation |= DEVELOPMENT_REMOTE_ORIENTATION_SOFTWARE;
            }
            lastOrientation = orientation;
        }
        lvgl_unlock();
    }
    return lastOrientation;
}

static const DevelopmentRemoteUiHandlers remoteUiHandlers = {
    .redraw = redrawScreen,
    .get_orientation = remoteOrientation,
};

// region Default services
namespace service {
    // Primary
    namespace audio { extern const ServiceManifest manifest; }
    namespace gpstime { extern const ServiceManifest manifest; }
#ifdef ESP_PLATFORM
    namespace rtctime { extern const ServiceManifest manifest; }
#endif

}

// endregion

void registerAndStartServices() {
    if (device_exists_of_type(&AUDIO_STREAM_TYPE)) {
        addService(service::audio::manifest);
    }
    check(module_ensure_started(&bluetooth_module) == ERROR_NONE);
    check(module_ensure_started(&wifi_module) == ERROR_NONE);
    location_configure_certificate(reinterpret_cast<const char*>(asset_certificates_GTS_Root_R4_pem));
    app_hub_configure_certificate(reinterpret_cast<const char*>(asset_certificates_GTS_Root_R4_pem));
    check(module_ensure_started(&location_module) == ERROR_NONE);
    check(module_ensure_started(&app_hub_module) == ERROR_NONE);
    // Before the mesh service starts, as it needs the region to turn on the radio
    settings::radio::applyRegion();
    check(module_ensure_started(&mesh_module) == ERROR_NONE);
    check(module_ensure_started(&mesh_meshcore_module) == ERROR_NONE);
    check(module_ensure_started(&mesh_meshtastic_module) == ERROR_NONE);
    development_remote_set_ui_handlers(&remoteUiHandlers);
    check(module_ensure_started(&development_module) == ERROR_NONE);
#if defined(__linux__) || defined(__APPLE__)
    check(posix_services_start() == ERROR_NONE);
#endif

    if (device_exists_of_type(&GPS_TYPE)) {
        addService(service::gpstime::manifest);
    }

#if defined(ESP_PLATFORM)
    if (device_exists_of_type(&RTC_TYPE)) {
        addService(service::rtctime::manifest);
    }
#endif
}

} // namespace
