#include <Tactility/network/NtpPrivate.h>
#include <Tactility/Preferences.h>

#include <tactility/log.h>

#include <memory>

#ifdef ESP_PLATFORM
#include <Tactility/SystemEvents.h>
#include <Tactility/TactilityCore.h>
#include <esp_netif_sntp.h>
#include <esp_sntp.h>
#endif

namespace tt::network::ntp {

constexpr auto* TAG = "NTP";

static bool processedSyncEvent = false;

#ifdef ESP_PLATFORM

void storeTimeInNvs() {
    time_t now;
    time(&now);

    auto preferences = std::make_unique<Preferences>("time");
    preferences->putInt64("syncTime", now);
    LOG_I(TAG, "Stored time %ld", (long)now);
}

void setTimeFromNvs() {
    auto preferences = std::make_unique<Preferences>("time");
    time_t synced_time;
    if (preferences->optInt64("syncTime", synced_time)) {
        LOG_I(TAG, "Restoring last known time to %ld", (long)synced_time);
        timeval get_nvs_time;
        get_nvs_time.tv_sec = synced_time;
        settimeofday(&get_nvs_time, nullptr);
    }
}

static void onTimeSynced(timeval* tv) {
    LOG_I(TAG, "Time synced (%ld)", (long)tv->tv_sec);
    processedSyncEvent = true;
    esp_netif_sntp_deinit();
    storeTimeInNvs();
    kernel::publishSystemEvent(kernel::SystemEvent::Time);
}

void init() {
    setTimeFromNvs();

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
