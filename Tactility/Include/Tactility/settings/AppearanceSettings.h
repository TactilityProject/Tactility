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

enum class DensityMode {
    /** Regular or compact, as configured for the device */
    DeviceDefault,
    Regular,
    Compact
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
    DensityMode densityMode = DensityMode::DeviceDefault;
    bool animationsEnabled = true;
    /** Theme colours as 0xRRGGBB, or empty for the theme's default colour */
    std::optional<uint32_t> primaryColor;
    std::optional<uint32_t> secondaryColor;
    std::optional<uint32_t> errorColor;
    /** Surface (screen background) colour as 0xRRGGBB, or empty for the theme's own surface. See getSurfaceColor(). */
    std::optional<uint32_t> surfaceColor;
    /** How strongly surfaceColor shows, from 0 (low) to SURFACE_TINT_LEVEL_COUNT - 1 (high) */
    uint8_t surfaceTintLevel = 1;

    bool operator==(const AppearanceSettings&) const = default;
};

constexpr uint8_t SURFACE_TINT_LEVEL_COUNT = 3;

/**
 * Mixes the surface colour into black (dark themes) or white (light themes), so the theme's text stays readable on it.
 * @return the colour that the theme uses as its surface, as 0xRRGGBB
 */
uint32_t getSurfaceColor(uint32_t color, uint8_t tintLevel, bool isDark);

bool load(AppearanceSettings& settings);

AppearanceSettings loadOrGetDefault();

bool save(const AppearanceSettings& settings);

}
