#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct BinFont;

namespace tt::lvgl {

/** @return the path of a font file in the system partition */
std::string getSystemFontPath(const char* fileName);

/** @return 1 for monochrome displays, otherwise 4 when PSRAM is available, otherwise 2 */
uint8_t getGeneratedFontBpp();

/** @return the cache file name for a generated font */
std::string getCachedFontFileName(const char* prefix, const char* name, uint16_t size, uint8_t bpp, uint32_t version);

/** Deletes the cached fonts that start with prefix, except for the ones in currentFileNames. */
void deleteStaleCachedFonts(const char* prefix, const std::vector<std::string>& currentFileNames);

/**
 * Opens a font from the font cache in the data path. When it's not cached, it's rasterized from
 * the TTF and written to the cache. Without a data path, the font is only kept in memory.
 * @param[in] fileName the cache file name, see getCachedFontFileName()
 * @param[in] ttfPath the TrueType font to rasterize from
 * @param[in] size font size in pixels
 * @param[in] bpp bits per pixel
 * @param[in] getCodepoints provides the codepoints to rasterize, only called when the font is generated.
 * An empty list rasterizes every codepoint in the TTF.
 * @return the font, or nullptr when it couldn't be loaded or generated
 */
BinFont* loadOrGenerateFont(const std::string& fileName, const std::string& ttfPath, uint16_t size, uint8_t bpp, const std::function<std::vector<uint32_t>()>& getCodepoints);

/**
 * Generates a font and writes it to the font cache, unless it's cached already.
 * Without a data path there is no cache: the font is generated and discarded, to verify that it can be.
 * @return false when the font couldn't be generated or written
 */
bool ensureCachedFont(const std::string& fileName, const std::string& ttfPath, uint16_t size, uint8_t bpp, const std::function<std::vector<uint32_t>()>& getCodepoints);

/** @return a version for a font file that changes when the file is replaced, or 0 when it doesn't exist */
uint32_t getFontFileVersion(const std::string& path);

}
