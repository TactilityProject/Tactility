#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace tt::settings::appearance {

enum class ThemeMode {
    /** Light or dark, as configured for the device */
    DeviceDefault,
    Light,
    Dark
};

struct AppearanceSettings {
    /** Default text font size in pixels, or 0 for the device's default size */
    uint16_t fontSize = 0;
    /** TrueType font for regular text, or empty for the system font */
    std::string regularFontPath;
    /** TrueType font for monospace text, or empty for the system font */
    std::string monoFontPath;
    ThemeMode themeMode = ThemeMode::DeviceDefault;
    /** Use the monochrome theme, also on colour displays */
    bool monoTheme = false;
    bool animationsEnabled = true;
    /** Theme colours as 0xRRGGBB, or empty for the theme's default colour */
    std::optional<uint32_t> primaryColor;
    std::optional<uint32_t> secondaryColor;
    std::optional<uint32_t> errorColor;

    bool operator==(const AppearanceSettings&) const = default;
};

bool load(AppearanceSettings& settings);

AppearanceSettings loadOrGetDefault();

bool save(const AppearanceSettings& settings);

}
