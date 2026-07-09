#ifdef ESP_PLATFORM

#include <Tactility/service/rtctime/RtcTimeService.h>

#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>

#include <tactility/device.h>
#include <tactility/drivers/rtc.h>
#include <tactility/log.h>

#include <cassert>
#include <ctime>
#include <sys/time.h>

namespace tt::service::rtctime {

constexpr auto* TAG = "RtcTime";

Device* RtcTimeService::findRtcDevice() {
    if (!rtcDevice) {
        rtcDevice = device_find_first_active_by_type(&RTC_TYPE);
    }
    return rtcDevice;
}

bool RtcTimeService::isAvailable() const {
    return rtcDevice != nullptr;
}

static bool setSystemTimeFromRtc(Device* rtc) {
    RtcDateTime dt = {};

    error_t err = rtc_get_time(rtc, &dt);
    if (err != ERROR_NONE) {
        LOG_E(TAG, "Failed to read RTC datetime");
        return false;
    }

    if (dt.year < 2024 || dt.year > 2199 ||
        dt.month < 1 || dt.month > 12 ||
        dt.day < 1 || dt.day > 31 ||
        dt.hour > 23 || dt.minute > 59 || dt.second > 59) {
        LOG_W(TAG, "Ignoring invalid RTC datetime: %02d-%02d-%02d %02d:%02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        return false;
    }

    struct tm tm_struct = {};
    tm_struct.tm_year = dt.year - 1900;
    tm_struct.tm_mon = dt.month - 1;
    tm_struct.tm_mday = dt.day;
    tm_struct.tm_hour = dt.hour;
    tm_struct.tm_min = dt.minute;
    tm_struct.tm_sec = dt.second;
    tm_struct.tm_isdst = -1;

    time_t t = mktime(&tm_struct);
    if (t == -1) {
        LOG_E(TAG, "Failed to convert RTC datetime to time_t");
        return false;
    }

    timeval tv = {.tv_sec = t, .tv_usec = 0};
    int result = settimeofday(&tv, nullptr);
    if (result != 0) {
        LOG_E(TAG, "settimeofday failed");
        return false;
    }

    LOG_I(TAG, "System time set from RTC: %02d-%02d-%02d %02d:%02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    return true;
}

static void writeRtcFromSystemTime(Device* rtc) {
    time_t now = time(nullptr);
    tm* tm_struct = localtime(&now);
    if (!tm_struct) {
        LOG_E(TAG, "localtime failed");
        return;
    }

    RtcDateTime dt = {
        .year = static_cast<uint16_t>(tm_struct->tm_year + 1900),
        .month = static_cast<uint8_t>(tm_struct->tm_mon + 1),
        .day = static_cast<uint8_t>(tm_struct->tm_mday),
        .hour = static_cast<uint8_t>(tm_struct->tm_hour),
        .minute = static_cast<uint8_t>(tm_struct->tm_min),
        .second = static_cast<uint8_t>(tm_struct->tm_sec)
    };

    error_t err = rtc_set_time(rtc, &dt);
    if (err != ERROR_NONE) {
        LOG_E(TAG, "Failed to write system time to RTC");
    } else {
        LOG_I(TAG, "RTC updated from NTP: %02d-%02d-%02d %02d:%02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    }
}

void RtcTimeService::onTimeChanged(kernel::SystemEvent event) {
    if (event == kernel::SystemEvent::Time) {
        Device* rtc = findRtcDevice();
        if (rtc) {
            writeRtcFromSystemTime(rtc);
        }
    }
}

bool RtcTimeService::onStart(ServiceContext& serviceContext) {
    Device* rtc = findRtcDevice();
    if (!rtc) {
        LOG_W(TAG, "No RTC device found");
        return true; // Continue without RTC
    }

    if (setSystemTimeFromRtc(rtc)) {
        // Publish time event so other components know time is now valid
        kernel::publishSystemEvent(kernel::SystemEvent::Time);
    }

    timeEventSubscription = kernel::subscribeSystemEvent(
        kernel::SystemEvent::Time,
        [this](kernel::SystemEvent event) { onTimeChanged(event); }
    );

    return true;
}

void RtcTimeService::onStop(ServiceContext& serviceContext) {
    if (timeEventSubscription != 0) {
        kernel::unsubscribeSystemEvent(timeEventSubscription);
        timeEventSubscription = 0;
    }
}

extern const ServiceManifest manifest = {
    .id = "RtcTime",
    .createService = create<RtcTimeService>
};

// Precondition: RtcTimeService must already be registered and started. RtcTime.cpp's
// isAvailable() does NOT use this (it must tolerate the service never having been
// registered on RTC-less devices) - this is for callers that require the service to exist.
std::shared_ptr<RtcTimeService> findRtcTimeService() {
    auto service = findServiceById(manifest.id);
    assert(service != nullptr);
    return std::static_pointer_cast<RtcTimeService>(service);
}

} // namespace tt::service::rtctime

#endif
