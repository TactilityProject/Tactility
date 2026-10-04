#include <Tactility/TactilityPrivate.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>

#include <wifi/module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/audio_stream.h>
#include <tactility/drivers/rtc.h>
#include <tactility/log.h>
#include <tactility/module.h>

namespace tt {

constexpr auto* TAG = "Tactility";

// region Default services
namespace service {
    // Primary
    namespace audio { extern const ServiceManifest manifest; }
    namespace development { extern const ServiceManifest manifest; }
#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLED)
    namespace espnow { extern const ServiceManifest manifest; }
#endif
#ifdef ESP_PLATFORM
    namespace rtctime { extern const ServiceManifest manifest; }
#endif
    namespace webserver { extern const ServiceManifest manifest; }

}

// endregion

void registerAndStartServices() {
    LOG_I(TAG, "Registering and starting primary system services");
    if (device_exists_of_type(&AUDIO_STREAM_TYPE)) {
        addService(service::audio::manifest);
    }
    check(module_ensure_started(&wifi_module) == ERROR_NONE);
    addService(service::development::manifest);
    addService(service::webserver::manifest);

#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_ESP_HOSTED_ENABLED)
    addService(service::espnow::manifest);
#endif
#if defined(ESP_PLATFORM)
    if (device_exists_of_type(&RTC_TYPE)) {
        addService(service::rtctime::manifest);
    }
#endif
}

} // namespace
