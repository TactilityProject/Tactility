#include "doctest.h"

#include <Tactility/service/gpstime/GpsTimeSyncService.h>

#include <ctime>

using namespace tt::service::gpstime;

TEST_CASE("gps time is applied when ntp has not synced") {
    CHECK(shouldApplyGpsTime(1700000000000, 1700000000000, false));
    CHECK(shouldApplyGpsTime(1700000000000, 0, false));
}

TEST_CASE("gps time only corrects a synced clock when it drifted") {
    const int64_t gps = 1700000000000;
    CHECK_FALSE(shouldApplyGpsTime(gps, gps, true));
    CHECK_FALSE(shouldApplyGpsTime(gps, gps + MAX_DRIFT_MS, true));
    CHECK_FALSE(shouldApplyGpsTime(gps, gps - MAX_DRIFT_MS, true));
    CHECK(shouldApplyGpsTime(gps, gps + MAX_DRIFT_MS + 1, true));
    CHECK(shouldApplyGpsTime(gps, gps - MAX_DRIFT_MS - 1, true));
}

#ifndef ESP_PLATFORM
TEST_CASE("gps time doesn't change the clock of the host") {
    const time_t before = time(nullptr);
    GpsTimeSyncService service;
    service.onLocationTime(static_cast<int64_t>(before + 86400) * 1000);
    CHECK(std::llabs(static_cast<long long>(time(nullptr) - before)) <= 1);
}
#endif
