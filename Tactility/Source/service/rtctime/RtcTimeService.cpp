#ifdef ESP_PLATFORM

#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/kernel/SystemEvents.h>
#include <Tactility/Logger.h>

#include <tactility/device.h>
#include <drivers/bm8563.h>

#include <ctime>
#include <sys/time.h>

namespace tt::service::rtctime {

static const auto LOGGER = Logger("RtcTime");

class RtcTimeService final : public Service {

    kernel::SystemEventSubscription time_event_subscription = 0;

    static Device* findRtcDevice() {
        Device* rtc = device_find_by_name("rtc");
        if (rtc) return rtc;
        // Fallback: search by compatible type
        // The bm8563 driver is compatible with "belling,bm8563", "nxp,pcf85063", "nxp,pcf8563"
        // In device tree it's typically named "rtc"
        return nullptr;
    }

    static bool setSystemTimeFromRtc(Device* rtc) {
        Bm8563DateTime dt;
        error_t err = bm8563_get_datetime(rtc, &dt);
        if (err != ERROR_NONE) {
            LOGGER.error("Failed to read RTC datetime");
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
            LOGGER.error("Failed to convert RTC datetime to time_t");
            return false;
        }

        timeval tv = { .tv_sec = t, .tv_usec = 0 };
        int result = settimeofday(&tv, nullptr);
        if (result != 0) {
            LOGGER.error("settimeofday failed");
            return false;
        }

        LOGGER.info("System time set from RTC: {}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                    dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        return true;
    }

    static void writeRtcFromSystemTime(Device* rtc) {
        time_t now = time(nullptr);
        tm* tm_struct = localtime(&now);
        if (!tm_struct) {
            LOGGER.error("localtime failed");
            return;
        }

        Bm8563DateTime dt = {
            .year = static_cast<uint16_t>(tm_struct->tm_year + 1900),
            .month = static_cast<uint8_t>(tm_struct->tm_mon + 1),
            .day = static_cast<uint8_t>(tm_struct->tm_mday),
            .hour = static_cast<uint8_t>(tm_struct->tm_hour),
            .minute = static_cast<uint8_t>(tm_struct->tm_min),
            .second = static_cast<uint8_t>(tm_struct->tm_sec)
        };

        error_t err = bm8563_set_datetime(rtc, &dt);
        if (err != ERROR_NONE) {
            LOGGER.error("Failed to write system time to RTC");
        } else {
            LOGGER.info("RTC updated from NTP: {}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                        dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        }
    }

    void onTimeChanged(kernel::SystemEvent event) {
        if (event == kernel::SystemEvent::Time) {
            Device* rtc = findRtcDevice();
            if (rtc) {
                writeRtcFromSystemTime(rtc);
            }
        }
    }

public:
    bool onStart(ServiceContext& service) override {
        Device* rtc = findRtcDevice();
        if (!rtc) {
            LOGGER.warn("No RTC device found");
            return true; // Continue without RTC
        }

        if (setSystemTimeFromRtc(rtc)) {
            // Publish time event so other components know time is now valid
            kernel::publishSystemEvent(kernel::SystemEvent::Time);
        }

        time_event_subscription = kernel::subscribeSystemEvent(
            kernel::SystemEvent::Time,
            [this](kernel::SystemEvent event) { onTimeChanged(event); }
        );

        return true;
    }

    void onStop(ServiceContext& service) override {
        if (time_event_subscription != 0) {
            kernel::unsubscribeSystemEvent(time_event_subscription);
            time_event_subscription = 0;
        }
    }
};

extern const ServiceManifest manifest = {
    .id = "RtcTime",
    .createService = create<RtcTimeService>
};

}

#endif
