#include <Tactility/network/NtpPrivate.h>

#include <tactility/log.h>
#include <tactility/paths.h>
#include <tactility/preferences.h>
#include <tactility/system_event.h>

#include <string>

#ifdef ESP_PLATFORM
#include <esp_netif_sntp.h>
#include <esp_sntp.h>
#endif

namespace tt::network::ntp {

constexpr auto* TAG = "NTP";

static bool processedSyncEvent = false;

#ifdef ESP_PLATFORM

static bool getPreferencesPath(std::string& outPath) {
    char root[128];
    if (paths_get_data_path(root, sizeof(root)) != ERROR_NONE) {
        return false;
    }
    outPath = std::string(root) + "/settings/ntp.properties";
    return true;
}

void storeTimeInNvs() {
    time_t now;
    time(&now);

    std::string path;
    if (!getPreferencesPath(path)) {
        return;
    }
    Preferences* preferences = preferences_open(path.c_str());
    if (preferences == nullptr) {
        return;
    }
    preferences_put_int64(preferences, "syncTime", now);
    preferences_close(preferences);
    LOG_I(TAG, "Stored time %ld", (long)now);
}

void setTimeFromNvs() {
    std::string path;
    if (!getPreferencesPath(path)) {
        return;
    }
    Preferences* preferences = preferences_open(path.c_str());
    if (preferences == nullptr) {
        return;
    }
    int64_t synced_time = 0;
    if (preferences_opt_int64(preferences, "syncTime", &synced_time)) {
        // The stored time is the minimum, as time doesn't go backwards
        if (synced_time > time(nullptr)) {
            LOG_I(TAG, "Restoring last known time to %ld", (long)synced_time);
            timeval get_nvs_time;
            get_nvs_time.tv_sec = synced_time;
            settimeofday(&get_nvs_time, nullptr);
        }
    }
    preferences_close(preferences);
}

static void onTimeSynced(timeval* tv) {
    LOG_I(TAG, "Time synced (%ld)", (long)tv->tv_sec);
    processedSyncEvent = true;
    esp_netif_sntp_deinit();
    storeTimeInNvs();
    system_event_emit(KERNEL_EVENT_TIME_CHANGED, nullptr, 0);
}

void init() {
    setTimeFromNvs();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.cloudflare.com");
    config.sync_cb = onTimeSynced;
    esp_netif_sntp_init(&config);
}

#else

void storeTimeInNvs() {}

void init() {
    processedSyncEvent = true;
}

#endif

bool isSynced() {
    return processedSyncEvent;
}

}
