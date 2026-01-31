#include <Tactility/settings/DisplaySettings.h>

#include <Tactility/file/PropertiesFile.h>
#include <tactility/hal/Device.h>
#include <Tactility/hal/display/DisplayDevice.h>

#include <map>
#include <string>
#include <utility>

namespace tt::settings::display {

constexpr auto* SETTINGS_FILE = "/data/settings/display.properties";
constexpr auto* SETTINGS_KEY_ORIENTATION = "orientation";
constexpr auto* SETTINGS_KEY_GAMMA_CURVE = "gammaCurve";
constexpr auto* SETTINGS_KEY_BACKLIGHT_DUTY = "backlightDuty";
constexpr auto* SETTINGS_KEY_TIMEOUT_ENABLED = "backlightTimeoutEnabled";
constexpr auto* SETTINGS_KEY_TIMEOUT_MS = "backlightTimeoutMs";
constexpr auto* SETTINGS_KEY_SCREENSAVER_TYPE = "screensaverType";

static Orientation getDefaultOrientation() {
    auto* display = lv_display_get_default();
    if (display == nullptr) {
        return Orientation::Landscape;
    }

    if (lv_display_get_physical_horizontal_resolution(display) > lv_display_get_physical_vertical_resolution(display)) {
        return Orientation::Landscape;
    } else {
        return Orientation::Portrait;
    }
}

static std::string toString(Orientation orientation) {
    switch (orientation) {
        using enum Orientation;
        case Portrait:
            return "Portrait";
        case Landscape:
            return "Landscape";
        case PortraitFlipped:
            return "PortraitFlipped";
        case LandscapeFlipped:
            return "LandscapeFlipped";
        default:
            std::unreachable();
    }
}

static bool fromString(const std::string& str, Orientation& orientation) {
    if (str == "Portrait") {
        orientation = Orientation::Portrait;
        return true;
    } else if (str == "Landscape") {
        orientation = Orientation::Landscape;
        return true;
    } else if (str == "PortraitFlipped") {
        orientation = Orientation::PortraitFlipped;
        return true;
    } else if (str == "LandscapeFlipped") {
        orientation = Orientation::LandscapeFlipped;
        return true;
    } else {
        return false;
    }
}

static std::string toString(ScreensaverType type) {
    switch (type) {
        using enum ScreensaverType;
        case None:
            return "None";
        case BouncingBalls:
            return "BouncingBalls";
        case Mystify:
            return "Mystify";
        case MatrixRain:
            return "MatrixRain";
        default:
            std::unreachable();
    }
}

static bool fromString(const std::string& str, ScreensaverType& type) {
    if (str == "None") {
        type = ScreensaverType::None;
        return true;
    } else if (str == "BouncingBalls") {
        type = ScreensaverType::BouncingBalls;
        return true;
    } else if (str == "Mystify") {
        type = ScreensaverType::Mystify;
        return true;
    } else if (str == "MatrixRain") {
        type = ScreensaverType::MatrixRain;
        return true;
    } else {
        return false;
    }
}

bool load(DisplaySettings& settings) {
    std::map<std::string, std::string> map;
    if (!file::loadPropertiesFile(SETTINGS_FILE, map)) {
        return false;
    }

    auto orientation_entry = map.find(SETTINGS_KEY_ORIENTATION);
    Orientation orientation;
    if (orientation_entry == map.end() || !fromString(orientation_entry->second, orientation)) {
        orientation = getDefaultOrientation();
    }

    auto gamma_entry = map.find(SETTINGS_KEY_GAMMA_CURVE);
    int gamma_curve = 0;
    if (gamma_entry != map.end()) {
        gamma_curve = atoi(gamma_entry->second.c_str());
    }

    auto backlight_duty_entry = map.find(SETTINGS_KEY_BACKLIGHT_DUTY);
    int backlight_duty = 200; // default
    if (backlight_duty_entry != map.end()) {
        backlight_duty = atoi(backlight_duty_entry->second.c_str());
        if (backlight_duty_entry->second != "0" && backlight_duty == 0) {
            backlight_duty = 200;
        }
    }

    bool timeout_enabled = false;
    auto timeout_enabled_entry = map.find(SETTINGS_KEY_TIMEOUT_ENABLED);
    if (timeout_enabled_entry != map.end()) {
        timeout_enabled = (timeout_enabled_entry->second == "1" || timeout_enabled_entry->second == "true" || timeout_enabled_entry->second == "True");
    }

    uint32_t timeout_ms = 60000; // default 60s
    auto timeout_ms_entry = map.find(SETTINGS_KEY_TIMEOUT_MS);
    if (timeout_ms_entry != map.end()) {
        timeout_ms = static_cast<uint32_t>(std::strtoul(timeout_ms_entry->second.c_str(), nullptr, 10));
    }

    auto screensaver_entry = map.find(SETTINGS_KEY_SCREENSAVER_TYPE);
    ScreensaverType screensaver_type = ScreensaverType::BouncingBalls;
    if (screensaver_entry != map.end()) {
        fromString(screensaver_entry->second, screensaver_type);
    }

    settings.orientation = orientation;
    settings.gammaCurve = gamma_curve;
    settings.backlightDuty = backlight_duty;
    settings.backlightTimeoutEnabled = timeout_enabled;
    settings.backlightTimeoutMs = timeout_ms;
    settings.screensaverType = screensaver_type;

    return true;
}

DisplaySettings getDefault() {
    return DisplaySettings {
        .orientation = getDefaultOrientation(),
        .gammaCurve = 1,
        .backlightDuty = 200,
        .backlightTimeoutEnabled = false,
        .backlightTimeoutMs = 60000,
        .screensaverType = ScreensaverType::BouncingBalls
    };
}

DisplaySettings loadOrGetDefault() {
    DisplaySettings settings;
    if (!load(settings)) {
        settings = getDefault();
    }
    return settings;
}

bool save(const DisplaySettings& settings) {
    std::map<std::string, std::string> map;
    map[SETTINGS_KEY_BACKLIGHT_DUTY] = std::to_string(settings.backlightDuty);
    map[SETTINGS_KEY_GAMMA_CURVE] = std::to_string(settings.gammaCurve);
    map[SETTINGS_KEY_ORIENTATION] = toString(settings.orientation);
    map[SETTINGS_KEY_TIMEOUT_ENABLED] = settings.backlightTimeoutEnabled ? "1" : "0";
    map[SETTINGS_KEY_TIMEOUT_MS] = std::to_string(settings.backlightTimeoutMs);
    map[SETTINGS_KEY_SCREENSAVER_TYPE] = toString(settings.screensaverType);
    return file::savePropertiesFile(SETTINGS_FILE, map);
}

lv_display_rotation_t toLvglDisplayRotation(Orientation orientation) {
    auto* lvgl_display = lv_display_get_default();
    auto rotation = lv_display_get_rotation(lvgl_display);
    bool is_originally_landscape;
    // The lvgl resolution code compensates for rotation. We have to revert the compensation to get the real display resolution
    // TODO: Use info from display driver
    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        is_originally_landscape = lv_display_get_physical_horizontal_resolution(lvgl_display) > lv_display_get_physical_vertical_resolution(lvgl_display);
    } else {
        is_originally_landscape = lv_display_get_physical_horizontal_resolution(lvgl_display) < lv_display_get_physical_vertical_resolution(lvgl_display);
    }
    if (is_originally_landscape) {
        // Landscape display
        switch (orientation) {
            case Orientation::Landscape:
                return LV_DISPLAY_ROTATION_0;
            case Orientation::Portrait:
                return LV_DISPLAY_ROTATION_90;
            case Orientation::LandscapeFlipped:
                return LV_DISPLAY_ROTATION_180;
            case Orientation::PortraitFlipped:
                return LV_DISPLAY_ROTATION_270;
            default:
                return LV_DISPLAY_ROTATION_0;
        }
    } else {
        // Portrait display
        switch (orientation) {
            case Orientation::Landscape:
                return LV_DISPLAY_ROTATION_90;
            case Orientation::Portrait:
                return LV_DISPLAY_ROTATION_0;
            case Orientation::LandscapeFlipped:
                return LV_DISPLAY_ROTATION_270;
            case Orientation::PortraitFlipped:
                return LV_DISPLAY_ROTATION_180;
            default:
                return LV_DISPLAY_ROTATION_0;
        }
    }
}

} // namespace
