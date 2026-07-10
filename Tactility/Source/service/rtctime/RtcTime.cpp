#include <Tactility/service/rtctime/RtcTime.h>

#ifdef ESP_PLATFORM
#include <Tactility/service/rtctime/RtcTimeService.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#endif

namespace tt::service::rtctime {

#ifdef ESP_PLATFORM
extern const ServiceManifest manifest;
#endif

bool isAvailable() {
#ifdef ESP_PLATFORM
    // The service is only registered when an RTC device is present (see Tactility.cpp);
    // treat "not registered" the same as "no device bound" rather than asserting.
    auto service = findServiceById<RtcTimeService>(manifest.id);
    return service != nullptr && service->isAvailable();
#else
    return false;
#endif
}

} // namespace tt::service::rtctime
