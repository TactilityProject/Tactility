#ifdef ESP_PLATFORM

#include <Tactility/service/rtctime/RtcTimeService.h>

#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>

#include <tactility/device.h>
#include <tactility/drivers/rtc.h>
#include <tactility/log.h>
#include <tactility/system_event.h>

#include <cassert>
#include <ctime>
#include <sys/time.h>

namespace tt::service::rtctime {

constexpr auto* TAG = "RtcTime";

Device* RtcTimeService::findRtcDevice() {
    if (!rtcDevice) {
        device_get_first_active_by_type(&RTC_TYPE, &rtcDevice);
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
    tm tm_struct = {};
    if (!localtime_r(&now, &tm_struct)) {
        LOG_E(TAG, "localtime_r failed");
        return;
    }

    RtcDateTime dt = {
        .year = static_cast<uint16_t>(tm_struct.tm_year + 1900),
        .month = static_cast<uint8_t>(tm_struct.tm_mon + 1),
        .day = static_cast<uint8_t>(tm_struct.tm_mday),
        .hour = static_cast<uint8_t>(tm_struct.tm_hour),
        .minute = static_cast<uint8_t>(tm_struct.tm_min),
        .second = static_cast<uint8_t>(tm_struct.tm_sec)
    };

    error_t err = rtc_set_time(rtc, &dt);
    if (err != ERROR_NONE) {
        LOG_E(TAG, "Failed to write system time to RTC");
    } else {
        LOG_I(TAG, "RTC updated from system time: %02d-%02d-%02d %02d:%02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    }
}

void RtcTimeService::onTimeChanged() {
    Device* rtc = findRtcDevice();
    if (rtc) {
        writeRtcFromSystemTime(rtc);
    }
}

void RtcTimeService::onTimeChangedTrampoline(struct SystemEvent* /*event*/, void* context) {
    static_cast<RtcTimeService*>(context)->onTimeChanged();
}

bool RtcTimeService::onStart(ServiceContext& serviceContext) {
    Device* rtc = findRtcDevice();
    if (!rtc) {
        LOG_W(TAG, "No RTC device found");
        return true; // Continue without RTC
    }

    if (setSystemTimeFromRtc(rtc)) {
        // Emit time event so other components know time is now valid
        system_event_emit(KERNEL_EVENT_TIME_CHANGED, nullptr, 0);
    }

    if (system_event_callback_add(KERNEL_EVENT_TIME_CHANGED, &RtcTimeService::onTimeChangedTrampoline, this) == ERROR_NONE) {
        timeEventSubscribed = true;
    }

    return true;
}

void RtcTimeService::onStop(ServiceContext& serviceContext) {
    if (timeEventSubscribed) {
        system_event_callback_remove(KERNEL_EVENT_TIME_CHANGED, &RtcTimeService::onTimeChangedTrampoline);
        timeEventSubscribed = false;
    }

    if (rtcDevice) {
        device_put(rtcDevice);
        rtcDevice = nullptr;
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
