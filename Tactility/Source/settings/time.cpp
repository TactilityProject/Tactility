#include <Tactility/settings/Time.h>

#include <Tactility/kernel/SystemEvents.h>
#include <Tactility/Preferences.h>
#include <Tactility/settings/SystemSettings.h>

#ifdef ESP_PLATFORM
#include <ctime>
#endif

namespace tt::settings {

constexpr auto* TIME_SETTINGS_NAMESPACE = "time";

constexpr auto* TIMEZONE_PREFERENCES_KEY_NAME = "tz_name";
constexpr auto* TIMEZONE_PREFERENCES_KEY_CODE = "tz_code";
constexpr auto* TIMEZONE_PREFERENCES_KEY_TIME24 = "tz_time24";

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
    Preferences preferences(TIME_SETTINGS_NAMESPACE);
    preferences.putString(TIMEZONE_PREFERENCES_KEY_NAME, name);
    preferences.putString(TIMEZONE_PREFERENCES_KEY_CODE, code);

#ifdef ESP_PLATFORM
    setenv("TZ", code.c_str(), 1);
    tzset();
#endif

    kernel::publishSystemEvent(kernel::SystemEvent::Time);
}

std::string getTimeZoneName() {
    Preferences preferences(TIME_SETTINGS_NAMESPACE);
    std::string result;
    if (preferences.optString(TIMEZONE_PREFERENCES_KEY_NAME, result)) {
        return result;
    } else {
        return "Europe/Amsterdam";
    }
}

std::string getTimeZoneCode() {
    Preferences preferences(TIME_SETTINGS_NAMESPACE);
    std::string result;
    if (preferences.optString(TIMEZONE_PREFERENCES_KEY_CODE, result)) {
        return result;
    } else {
        return "CET-1CEST,M3.5.0,M10.5.0/3";  // Default: Europe/Amsterdam
    }
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
}

}
