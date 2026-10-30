#include <Tactility/service/gpstime/GpsTimeSyncService.h>

#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/network/Ntp.h>
#include <Tactility/settings/Time.h>

#include <gps/gps.h>
#include <tactility/device.h>
#include <tactility/log.h>
#include <tactility/system_event.h>

#include <cstring>
#include <ctime>
#include <sys/time.h>

namespace tt::service::gpstime {

constexpr auto* TAG = "GpsTimeSync";
constexpr TickType_t TIMER_INTERVAL_TICKS = 2000 / portTICK_PERIOD_MS;

bool GpsTimeSyncService::setSystemTime(int64_t timeMs) {
#ifdef ESP_PLATFORM
    const timeval gps = {
        .tv_sec = static_cast<time_t>(timeMs / 1000),
        .tv_usec = static_cast<suseconds_t>((timeMs % 1000) * 1000)
    };
    if (settimeofday(&gps, nullptr) != 0) {
        LOG_E(TAG, "settimeofday failed");
        return false;
    }
    return true;
#else
    // The simulator must not change the clock of the host
    LOG_W(TAG, "Not setting the system time to %lld on this platform", static_cast<long long>(timeMs));
    return false;
#endif
}

void GpsTimeSyncService::onLocationTime(int64_t timeMs) {
    if (done.exchange(true)) {
        return;
    }

    timeval now = {};
    gettimeofday(&now, nullptr);
    const int64_t systemTimeMs = static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_usec / 1000;
    if (!shouldApplyGpsTime(timeMs, systemTimeMs, network::ntp::isSynced())) {
        LOG_I(TAG, "System time is within %lld ms of GPS time", static_cast<long long>(MAX_DRIFT_MS));
        return;
    }

    if (!setSystemTime(timeMs)) {
        return;
    }
    LOG_I(TAG, "System time set from GPS (%lld)", static_cast<long long>(timeMs / 1000));
    network::ntp::storeTimeInNvs();
    system_event_emit(KERNEL_EVENT_TIME_CHANGED, nullptr, 0);
}

void GpsTimeSyncService::onTimer() {
    // GPS only needs to run until the time was announced
    if (done && subscribed.exchange(false)) {
        location_unsubscribe(&subscription);
    }
}

void GpsTimeSyncService::onLocationTimeTrampoline(struct SystemEvent* event, void* context) {
    LocationTimeEvent data;
    memcpy(&data, event->data, sizeof(data));
    static_cast<GpsTimeSyncService*>(context)->onLocationTime(data.time_ms);
}

bool GpsTimeSyncService::onStart(ServiceContext& /*serviceContext*/) {
    Device* gps = nullptr;
    if (!settings::isGpsTimeSyncAtBoot() || device_get_first_by_type(&GPS_TYPE, &gps) != ERROR_NONE) {
        return true;
    }
    device_put(gps);

    if (system_event_callback_add(KERNEL_EVENT_LOCATION_TIME, &GpsTimeSyncService::onLocationTimeTrampoline, this) != ERROR_NONE) {
        LOG_E(TAG, "Failed to listen for GPS time");
        return true;
    }
    eventSubscribed = true;

    // Fixes only come in while something is subscribed to the location, which also needs location services to be enabled
    if (!location_is_enabled()) {
        LOG_W(TAG, "Location services are disabled, waiting for another app to use them");
        return true;
    }
    task_event_group_construct(&eventGroup);
    eventGroupConstructed = true;
    if (location_subscribe(&subscription, &eventGroup, "Time sync") == ERROR_NONE) {
        subscribed = true;
        timer = std::make_unique<Timer>(Timer::Type::Periodic, TIMER_INTERVAL_TICKS, [this] { onTimer(); });
        timer->start();
    }
    return true;
}

void GpsTimeSyncService::onStop(ServiceContext& /*serviceContext*/) {
    if (timer) {
        timer->stop();
        timer = nullptr;
    }
    if (eventSubscribed) {
        system_event_callback_remove(KERNEL_EVENT_LOCATION_TIME, &GpsTimeSyncService::onLocationTimeTrampoline);
        eventSubscribed = false;
    }
    if (subscribed.exchange(false)) {
        location_unsubscribe(&subscription);
    }
    if (eventGroupConstructed) {
        task_event_group_destruct(&eventGroup);
        eventGroupConstructed = false;
    }
}

extern const ServiceManifest manifest = {
    .id = "tactility.gpstime",
    .createService = create<GpsTimeSyncService>
};

} // namespace tt::service::gpstime
