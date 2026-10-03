#include <Tactility/settings/LedStripSettings.h>

#include <Tactility/file/File.h>
#include <Tactility/file/PropertiesFile.h>

#include <app/paths.h>

#include <charconv>
#include <algorithm>
#include <iterator>
#include <map>
#include <string>

namespace tt::settings::ledstrip {

namespace {

struct ColorPreset {
    const char* name;
    LedRgb color;
};

const ColorPreset COLOR_PRESETS[] = {
    {"White",   LED_RGB_COLOR_WHITE},
    {"Green",   LED_RGB_COLOR_GREEN},
    {"Red",     LED_RGB_COLOR_RED},
    {"Blue",    LED_RGB_COLOR_BLUE},
    {"Yellow",  LED_RGB_COLOR_YELLOW},
    {"Orange",  LED_RGB_COLOR_ORANGE},
    {"Cyan",    LED_RGB_COLOR_CYAN},
    {"Magenta", LED_RGB_COLOR_MAGENTA},
    {"Purple",  LED_RGB_COLOR_PURPLE},
    {"Pink",    LED_RGB_COLOR_PINK},
    {"Teal",    LED_RGB_COLOR_TEAL},
    {"Black",   LED_RGB_COLOR_BLACK},
};

LedRgb getColor(uint8_t preset, const LedRgb& custom) {
    if (preset == 12) return custom;
    return COLOR_PRESETS[std::min<size_t>(preset, std::size(COLOR_PRESETS) - 1)].color;
}

uint8_t interpolate(uint8_t first, uint8_t second, size_t position, size_t count) {
    if (count <= 1) return first;
    const size_t denominator = count - 1;
    return static_cast<uint8_t>((first * (denominator - position) + second * position) / denominator);
}

}

static std::string getSettingsFilePath(const char* deviceName) {
    char path[256];
    const auto fileName = std::string(deviceName) + ".properties";
    if (app_paths_get_user_data_path("tactility.ledstripsettings", fileName.c_str(), path, sizeof(path)) != ERROR_NONE) {
        return "";
    }
    return path;
}

static bool readNumber(const std::map<std::string, std::string>& properties, const char* key, unsigned maximum, unsigned& value) {
    const auto it = properties.find(key);
    if (it == properties.end()) {
        return false;
    }

    unsigned parsed = 0;
    const auto result = std::from_chars(it->second.data(), it->second.data() + it->second.size(), parsed);
    if (result.ec != std::errc() || result.ptr != it->second.data() + it->second.size() || parsed > maximum) {
        return false;
    }

    value = parsed;
    return true;
}

static bool readBool(const std::map<std::string, std::string>& properties, const char* key, bool& value) {
    const auto it = properties.find(key);
    if (it == properties.end()) {
        return false;
    }
    if (it->second == "1" || it->second == "true") {
        value = true;
        return true;
    }
    if (it->second == "0" || it->second == "false") {
        value = false;
        return true;
    }
    return false;
}

LedStripSettings getDefault() {
    return {};
}

bool load(const char* deviceName, LedStripSettings& settings) {
    const auto path = getSettingsFilePath(deviceName);
    if (!file::isFile(path)) {
        return false;
    }

    std::map<std::string, std::string> properties;
    if (!file::loadPropertiesFile(path, properties)) {
        return false;
    }

    auto loaded = getDefault();
    unsigned value = 0;
    readBool(properties, "enabled", loaded.enabled);
    if (readNumber(properties, "pattern", static_cast<unsigned>(Pattern::Gradient), value)) {
        loaded.pattern = static_cast<Pattern>(value);
    }

    if (readNumber(properties, "brightness", UINT8_MAX, value)) loaded.brightness = static_cast<uint8_t>(value);
    if (readNumber(properties, "primaryPreset", 12, value)) loaded.primaryPreset = static_cast<uint8_t>(value);
    if (readNumber(properties, "secondaryPreset", 12, value)) loaded.secondaryPreset = static_cast<uint8_t>(value);

    if (readNumber(properties, "primaryRed", UINT8_MAX, value)) loaded.primaryCustom.r = static_cast<uint8_t>(value);
    if (readNumber(properties, "primaryGreen", UINT8_MAX, value)) loaded.primaryCustom.g = static_cast<uint8_t>(value);
    if (readNumber(properties, "primaryBlue", UINT8_MAX, value)) loaded.primaryCustom.b = static_cast<uint8_t>(value);
    if (readNumber(properties, "secondaryRed", UINT8_MAX, value)) loaded.secondaryCustom.r = static_cast<uint8_t>(value);
    if (readNumber(properties, "secondaryGreen", UINT8_MAX, value)) loaded.secondaryCustom.g = static_cast<uint8_t>(value);
    if (readNumber(properties, "secondaryBlue", UINT8_MAX, value)) loaded.secondaryCustom.b = static_cast<uint8_t>(value);

    settings = loaded;
    return true;
}

LedStripSettings loadOrGetDefault(const char* deviceName) {
    auto settings = getDefault();
    load(deviceName, settings);
    return settings;
}

bool save(const char* deviceName, const LedStripSettings& settings) {
    std::map<std::string, std::string> properties;
    properties["enabled"] = settings.enabled ? "1" : "0";
    properties["pattern"] = std::to_string(static_cast<unsigned>(settings.pattern));
    properties["brightness"] = std::to_string(settings.brightness);
    properties["primaryPreset"] = std::to_string(settings.primaryPreset);
    properties["secondaryPreset"] = std::to_string(settings.secondaryPreset);
    properties["primaryRed"] = std::to_string(settings.primaryCustom.r);
    properties["primaryGreen"] = std::to_string(settings.primaryCustom.g);
    properties["primaryBlue"] = std::to_string(settings.primaryCustom.b);
    properties["secondaryRed"] = std::to_string(settings.secondaryCustom.r);
    properties["secondaryGreen"] = std::to_string(settings.secondaryCustom.g);
    properties["secondaryBlue"] = std::to_string(settings.secondaryCustom.b);

    auto settings_path = getSettingsFilePath(deviceName);
    if (!file::findOrCreateParentDirectory(settings_path, 0755)) {
        return false;
    }

    return file::savePropertiesFile(settings_path, properties);
}

error_t apply(struct Device* device, const LedStripSettings& settings) {
    if (!settings.enabled) {
        return led_strip_clear(device);
    }

    uint16_t ledCount = 0;
    auto result = led_strip_get_length(device, &ledCount);
    if (result != ERROR_NONE) return result;
    if (ledCount == 0) return ERROR_INVALID_ARGUMENT;

    result = led_strip_set_brightness(device, settings.brightness);
    if (result != ERROR_NONE) return result;

    const LedRgb primary = getColor(settings.primaryPreset, settings.primaryCustom);
    const LedRgb secondary = getColor(settings.secondaryPreset, settings.secondaryCustom);
    if (settings.pattern == Pattern::Solid) {
        result = led_strip_fill_led_range_color(device, 0, ledCount, primary);
        if (result != ERROR_NONE) return result;
    } else {
        for (size_t index = 0; index < ledCount; index++) {
            LedRgb color = primary;
            if (settings.pattern == Pattern::Alternating) {
                color = (index % 2 == 0) ? primary : secondary;
            } else if (settings.pattern == Pattern::Gradient) {
                color = {
                    interpolate(primary.r, secondary.r, index, ledCount),
                    interpolate(primary.g, secondary.g, index, ledCount),
                    interpolate(primary.b, secondary.b, index, ledCount),
                };
            }
            result = led_strip_set_single_led_color(device, index, color);
            if (result != ERROR_NONE) return result;
        }
    }
    return led_strip_show(device);
}

}