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
#include <bluetooth/module.h>
#include <wifi/module.h>
#include <mesh/module.h>
#include <mesh_meshcore/module.h>
#include <mesh_meshtastic/module.h>

#if defined(__linux__) || defined(__APPLE__)
#include <tactility/posix_services.h>
#endif

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/audio_stream.h>
#include <tactility/drivers/rtc.h>
#include <gps/gps.h>
#include <tactility/module.h>

namespace tt {

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
