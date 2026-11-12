#include <service/paths.h>

#include <Tactility/settings/RadioSettings.h>
#include <Tactility/settings/Time.h>
#include <Tactility/settings/SystemSettings.h>

#include <tactility/paths.h>
#include <tactility/preferences.h>
#include <tactility/log.h>
#include <tactility/system_event.h>

#include <lvgl/widgets/statusbar.h>

#ifdef ESP_PLATFORM
#include <ctime>
#endif

namespace tt::settings {

constexpr auto* TAG = "Time";

constexpr auto* TIMEZONE_PREFERENCES_KEY_NAME = "tz_name";
constexpr auto* TIMEZONE_PREFERENCES_KEY_CODE = "tz_code";
constexpr auto* TIMEZONE_PREFERENCES_KEY_TIME24 = "tz_time24";
constexpr auto* GPS_SYNC_AT_BOOT_KEY = "gps_sync_boot";

namespace {

// Same "time" namespace/file that Ntp.cpp's storeTimeInNvs()/setTimeFromNvs() use for
// "syncTime" - matches the shared NVS namespace this used to be.
bool getPreferencesPath(std::string& outPath) {
    char root[128];
    // Not really a service, but this is the best way of organising it for now
    if (paths_get_data_path(root, sizeof(root)) != ERROR_NONE) {
        return false;
    }
    outPath = std::string(root) + "/settings/time.properties";
    return true;
}

} // namespace

void initTimeZone() {
#ifdef ESP_PLATFORM
    auto code= getTimeZoneCode();
    if (!code.empty()) {
        setenv("TZ", code.c_str(), 1);
        tzset();
    }
#endif
}

void setTimeZone(const std::string& name, const std::string& code) {
    std::string path;
    if (getPreferencesPath(path)) {
        Preferences* preferences = preferences_open(path.c_str());
        if (preferences != nullptr) {
            preferences_put_string(preferences, TIMEZONE_PREFERENCES_KEY_NAME, name.c_str());
            preferences_put_string(preferences, TIMEZONE_PREFERENCES_KEY_CODE, code.c_str());
            preferences_close(preferences);
        }
    }

#ifdef ESP_PLATFORM
    setenv("TZ", code.c_str(), 1);
    tzset();
#endif

    // The time zone tells where the device is, and so which radio band it may use
    if (radio::hasRadio()) {
        const RadioRegion region = radio::findRegionForTimeZone(name);
        if (region != RADIO_REGION_UNSET && !radio::saveRegion(region)) {
            LOG_E(TAG, "Failed to save the radio region");
        }
    }

    system_event_emit(KERNEL_EVENT_TIME_CHANGED, nullptr, 0);
}

std::string getTimeZoneName() {
    std::string path;
    if (getPreferencesPath(path)) {
        Preferences* preferences = preferences_open(path.c_str());
        if (preferences != nullptr) {
            char buffer[64];
            error_t error = preferences_opt_string(preferences, TIMEZONE_PREFERENCES_KEY_NAME, buffer, sizeof(buffer));
            preferences_close(preferences);
            if (error == ERROR_NONE) {
                return buffer;
            }
        }
    }
    return "Europe/Amsterdam";
}

bool hasTimeZone() {
    std::string path;
    if (!getPreferencesPath(path)) {
        return false;
    }
    Preferences* preferences = preferences_open(path.c_str());
    if (preferences == nullptr) {
        return false;
    }
    bool has = preferences_has_string(preferences, TIMEZONE_PREFERENCES_KEY_NAME);
    preferences_close(preferences);
    return has;
}

std::string getTimeZoneCode() {
    std::string path;
    if (getPreferencesPath(path)) {
        Preferences* preferences = preferences_open(path.c_str());
        if (preferences != nullptr) {
            char buffer[64];
            error_t error = preferences_opt_string(preferences, TIMEZONE_PREFERENCES_KEY_CODE, buffer, sizeof(buffer));
            preferences_close(preferences);
            if (error == ERROR_NONE) {
                return buffer;
            }
        }
    }
    return "CET-1CEST,M3.5.0,M10.5.0/3";  // Default: Europe/Amsterdam
}

bool isTimeFormat24Hour() {
    SystemSettings properties;
    if (!loadSystemSettings(properties)) {
        return true;
    } else {
        return properties.timeFormat24h;
    }
}

void setTimeFormat24Hour(bool show24Hour) {
    SystemSettings properties;
    if (!loadSystemSettings(properties)) {
        return;
    }

    properties.timeFormat24h = show24Hour;
    saveSystemSettings(properties);
    lvgl_statusbar_set_time_format_24h(show24Hour);
}

bool isGpsTimeSyncAtBoot() {
    std::string path;
    if (!getPreferencesPath(path)) {
        return false;
    }
    Preferences* preferences = preferences_open(path.c_str());
    if (preferences == nullptr) {
        return false;
    }
    bool enabled = false;
    preferences_opt_bool(preferences, GPS_SYNC_AT_BOOT_KEY, &enabled);
    preferences_close(preferences);
    return enabled;
}

void setGpsTimeSyncAtBoot(bool enabled) {
    std::string path;
    if (!getPreferencesPath(path)) {
        return;
    }
    Preferences* preferences = preferences_open(path.c_str());
    if (preferences != nullptr) {
        preferences_put_bool(preferences, GPS_SYNC_AT_BOOT_KEY, enabled);
        preferences_close(preferences);
    }
}

}
