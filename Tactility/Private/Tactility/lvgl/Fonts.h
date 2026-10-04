#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct BinFont;

namespace tt::lvgl {

/** Determines all generated fonts: text, icon and monospace */
struct FontConfiguration {
    /** Default text font size in pixels, other font sizes are derived from it */
    uint16_t defaultSize;
    /** TrueType font for regular text, or empty for the system font */
    std::string regularFontPath;
    /** TrueType font for monospace text, or empty for the system font */
    std::string monoFontPath;

    bool operator==(const FontConfiguration&) const = default;
};

/**
 * Fonts with more characters than this can take long to generate and use a lot of memory, as all
 * characters of a TTF are rasterized.
 */
constexpr size_t LARGE_FONT_CHARACTER_COUNT = 1000;

/** @return the number of characters in a TrueType font, or 0 when it can't be read */
size_t getTtfCharacterCount(const std::string& path);

/** @return the configuration from the appearance settings, with the device defaults for unset values */
FontConfiguration loadFontConfiguration();

/** @return the configuration of the loaded fonts, or loadFontConfiguration() before fonts are loaded */
FontConfiguration getFontConfiguration();

/**
 * Generates the cached fonts for a configuration that aren't cached yet. Doesn't affect the loaded fonts.
 * @return false when a font couldn't be generated, e.g. because a TTF file is invalid
 */
bool updateFontCache(const FontConfiguration& configuration);

/**
 * Loads the text and icon fonts for a configuration and registers them with lvgl-module, replacing
 * the previously loaded fonts. Fonts that aren't cached are generated. A custom TTF that fails is
 * replaced by the system font. Cached fonts of other configurations are deleted.
 * LVGL must not be running.
 */
void loadFonts(const FontConfiguration& configuration);

/**
 * Loads the monospace font of the current configuration (ASCII), generating and caching it when needed.
 * @return the font, to be released with binfont_close(), or nullptr when it couldn't be loaded
 */
BinFont* loadMonoFont();

}
