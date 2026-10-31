#pragma once

#include <Tactility/Timer.h>
#include <Tactility/service/Service.h>

#include <location/location.h>
#include <tactility/concurrent/task_event_group.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>

struct SystemEvent;

namespace tt::service::gpstime {

/** Largest difference between the system clock and GPS time that is left alone once NTP has synced */
constexpr int64_t MAX_DRIFT_MS = 5000;

/** @return true when the system clock should be set to GPS time */
inline bool shouldApplyGpsTime(int64_t gpsTimeMs, int64_t systemTimeMs, bool ntpSynced) {
    return !ntpSynced || std::llabs(gpsTimeMs - systemTimeMs) > MAX_DRIFT_MS;
}

/**
 * Sets the system clock from the first GPS time announced after boot.
 * Only runs when the user enabled settings::isGpsTimeSyncAtBoot() and the device has a GPS.
 */
class GpsTimeSyncService final : public Service {

    TaskEventGroup eventGroup {};
    LocationSubscription subscription {};
    bool eventGroupConstructed = false;
    bool eventSubscribed = false;
    std::atomic<bool> subscribed = false;
    std::atomic<bool> done = false;
    std::unique_ptr<Timer> timer;

    static bool setSystemTime(int64_t timeMs);
    void onTimer();
    static void onLocationTimeTrampoline(struct SystemEvent* event, void* context);

public:

    /** Handles the first announced time after boot, later calls are ignored */
    void onLocationTime(int64_t timeMs);

    bool onStart(ServiceContext& serviceContext) override;
    void onStop(ServiceContext& serviceContext) override;
};

} // namespace tt::service::gpstime
