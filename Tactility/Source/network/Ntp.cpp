#include <Tactility/network/NtpPrivate.h>
#include <Tactility/Logger.h>
#include <Tactility/Preferences.h>

#include <memory>

#ifdef ESP_PLATFORM
#include <Tactility/kernel/SystemEvents.h>
#include <Tactility/TactilityCore.h>
#include <tactility/concurrent/thread.h>
#include <tactility/device.h>
#include <drivers/bm8563.h>
#include <esp_netif_sntp.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <ctime>
#endif

namespace tt::network::ntp {

static const auto LOGGER = Logger("NTP");

static bool processedSyncEvent = false;

#ifdef ESP_PLATFORM

static Thread* rtcSyncThread = nullptr;
static bool stopRtcSync = false;

static void syncRtcFromSystemTime() {
    Device* rtc = device_find_by_name("rtc");
    if (!rtc) {
        return;
    }

    time_t now = time(nullptr);
    tm* tm_struct = localtime(&now);
    if (!tm_struct) {
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

    Bm8563DateTime rtc_dt;
    if (bm8563_get_datetime(rtc, &rtc_dt) != ERROR_NONE) {
        return;
    }

    tm* rtc_tm = nullptr;
    tm rtc_tm_struct = {};
    rtc_tm_struct.tm_year = rtc_dt.year - 1900;
    rtc_tm_struct.tm_mon = rtc_dt.month - 1;
    rtc_tm_struct.tm_mday = rtc_dt.day;
    rtc_tm_struct.tm_hour = rtc_dt.hour;
    rtc_tm_struct.tm_min = rtc_dt.minute;
    rtc_tm_struct.tm_sec = rtc_dt.second;
    rtc_tm_struct.tm_isdst = -1;
    time_t rtc_time = mktime(&rtc_tm_struct);

    if (rtc_time == now) {
        LOGGER.info("System time and RTC already equal, stopping sync");
        stopRtcSync = true;
        return;
    }

    error_t err = bm8563_set_datetime(rtc, &dt);
    if (err == ERROR_NONE) {
        LOGGER.info("Synced RTC from NTP: {}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                    dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    }
}

static int32_t rtcSyncTask(void* context) {
    LOGGER.info("RTC sync task started");
    while (!stopRtcSync) {
        if (processedSyncEvent) {
            syncRtcFromSystemTime();
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    LOGGER.info("RTC sync task stopped");
    return 0;
}

void storeTimeInNvs() {
    time_t now;
    time(&now);

    auto preferences = std::make_unique<Preferences>("time");
    preferences->putInt64("syncTime", now);
    LOGGER.info("Stored time {}", now);
}

void setTimeFromNvs() {
    auto preferences = std::make_unique<Preferences>("time");
    time_t synced_time;
    if (preferences->optInt64("syncTime", synced_time)) {
        LOGGER.info("Restoring last known time to {}", synced_time);
        timeval get_nvs_time;
        get_nvs_time.tv_sec = synced_time;
        settimeofday(&get_nvs_time, nullptr);
    }
}

static void onTimeSynced(timeval* tv) {
    LOGGER.info("Time synced ({})", tv->tv_sec);
    processedSyncEvent = true;
    esp_netif_sntp_deinit();
    storeTimeInNvs();
    kernel::publishSystemEvent(kernel::SystemEvent::Time);
}

void init() {
    setTimeFromNvs();

    stopRtcSync = false;
    rtcSyncThread = thread_alloc_full(
        "ntp_rtc_sync",
        4096,
        rtcSyncTask,
        nullptr,
        tskNO_AFFINITY
    );
    thread_set_priority(rtcSyncThread, THREAD_PRIORITY_LOW);
    thread_start(rtcSyncThread);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.cloudflare.com");
    config.sync_cb = onTimeSynced;
    esp_netif_sntp_init(&config);
}

#else

void init() {
    processedSyncEvent = true;
}

#endif

bool isSynced() {
    return processedSyncEvent;
}

}
