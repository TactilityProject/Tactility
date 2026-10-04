#pragma once

#include <cstdint>
#include <string>

namespace tt::settings::appearance {

struct AppearanceSettings {
    /** Default text font size in pixels, or 0 for the device's default size */
    uint16_t fontSize = 0;
    /** TrueType font for regular text, or empty for the system font */
    std::string regularFontPath;
    /** TrueType font for monospace text, or empty for the system font */
    std::string monoFontPath;

    bool operator==(const AppearanceSettings&) const = default;
};

bool load(AppearanceSettings& settings);

AppearanceSettings loadOrGetDefault();

bool save(const AppearanceSettings& settings);

}
