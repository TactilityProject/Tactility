#include <Tactility/lvgl/Fonts.h>

#include <Tactility/lvgl/FontCache.h>
#include <Tactility/lvgl/FontSizes.h>
#include <Tactility/lvgl/FontVersions.h>
#include <Tactility/settings/AppearanceSettings.h>

#include <generated_assets.h>

#include <binfont/binfont.h>
#include <binfont/generator.h>
#include <lvgl/binfont.h>
#include <lvgl/fonts.h>
#include <lvgl/icons/names.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/log.h>
#include <tactility/memory.h>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace tt::lvgl {

constexpr auto* TAG = "Fonts";
constexpr auto* TEXT_CACHE_PREFIX = "text_";
constexpr auto* MONO_CACHE_PREFIX = "mono_";
constexpr auto* ICON_CACHE_PREFIX = "material_symbols_";

using CodepointProvider = std::function<std::vector<uint32_t>()>;

/** A TTF and the codepoints to rasterize from it */
struct FontSource {
    TtfSource ttf;
    uint32_t version;
    /** An empty list rasterizes all codepoints of the TTF */
    CodepointProvider getCodepoints;
    bool custom;
};

/** A font in the font cache */
struct FontRequest {
    std::string fileName;
    TtfSource ttf;
    uint16_t size;
    uint8_t bpp;
    CodepointProvider getCodepoints;
};

struct TextFontDefinition {
    LvglFontSize id;
    const char* name;
};

struct IconFontDefinition {
    LvglIconFont id;
    const char* name;
    const char* const* iconNames;
    size_t iconNameCount;
};

constexpr TextFontDefinition TEXT_FONTS[] = {
    { FONT_SIZE_SMALL, "small" },
    { FONT_SIZE_DEFAULT, "default" },
    { FONT_SIZE_LARGE, "large" },
};

static const IconFontDefinition ICON_FONTS[] = {
    { LVGL_ICON_FONT_STATUSBAR, "statusbar", lvgl_icon_statusbar_names, lvgl_icon_statusbar_name_count },
    { LVGL_ICON_FONT_LAUNCHER, "launcher", lvgl_icon_launcher_names, lvgl_icon_launcher_name_count },
    { LVGL_ICON_FONT_SHARED_DEFAULT, "shared_default", lvgl_icon_shared_names, lvgl_icon_shared_name_count },
    { LVGL_ICON_FONT_SHARED_LARGE, "shared_large", lvgl_icon_shared_names, lvgl_icon_shared_name_count },
};

struct LoadedFont {
    BinFont* binFont;
    lv_font_t* lvFont;
};

/** ESP32 devices without PSRAM skip the large fonts to save memory, unless the fonts are part of the firmware, which doesn't use RAM */
static bool hasLimitedMemory() {
#ifdef ESP_PLATFORM
    return memory_external_total() == 0 && !hasGeneratedFonts();
#else
    return false;
#endif
}

/** The default text font replaces the large one when it isn't generated */
static bool isTextFontGenerated(LvglFontSize fontSize) {
    return fontSize != FONT_SIZE_LARGE || !hasLimitedMemory();
}

/** The shared icon font replaces the 2x one when it isn't generated */
static bool isIconFontGenerated(LvglIconFont iconFont) {
    return iconFont != LVGL_ICON_FONT_SHARED_LARGE || !hasLimitedMemory();
}

static std::vector<LoadedFont> loadedFonts;
static std::optional<FontConfiguration> activeConfiguration;

static Mutex& getConfigurationMutex() {
    static Mutex mutex = [] {
        Mutex result;
        mutex_construct(&result);
        return result;
    }();
    return mutex;
}

size_t getTtfCharacterCount(const std::string& path) {
    uint32_t* codepoints = nullptr;
    size_t count = 0;
    if (binfont_get_ttf_codepoints(path.c_str(), &codepoints, &count) != ERROR_NONE) {
        return 0;
    }
    memory_free(codepoints);
    return count;
}

static FontSource getSystemSource(const uint8_t* systemData, size_t systemSize, uint32_t systemVersion) {
    return { { "", systemData, systemSize }, systemVersion, [] { return std::vector<uint32_t>(); }, false };
}

static FontSource getSource(const std::string& customPath, const uint8_t* systemData, size_t systemSize, uint32_t systemVersion) {
    if (!customPath.empty()) {
        const uint32_t version = getFontFileVersion(customPath);
        if (version != 0) {
            auto get_codepoints = [customPath] {
                const size_t count = getTtfCharacterCount(customPath);
                if (count > LARGE_FONT_CHARACTER_COUNT) {
                    LOG_W(TAG, "%s has %zu characters, generating it can take long and use a lot of memory", customPath.c_str(), count);
                }
                return std::vector<uint32_t>();
            };
            return { { customPath }, version, get_codepoints, true };
        }
        LOG_W(TAG, "%s not found, using the system font", customPath.c_str());
    }
    return getSystemSource(systemData, systemSize, systemVersion);
}

static FontSource getTextSource(const FontConfiguration& configuration) {
    return getSource(configuration.regularFontPath, asset_fonts_AdwaitaSans_ttf, asset_fonts_AdwaitaSans_ttf_size, TT_TEXT_FONT_VERSION);
}

static FontSource getMonoSource(const FontConfiguration& configuration) {
    return getSource(configuration.monoFontPath, asset_fonts_AdwaitaMono_ttf, asset_fonts_AdwaitaMono_ttf_size, TT_MONO_FONT_VERSION);
}

static FontRequest getTextRequest(const FontConfiguration& configuration, const FontSource& source, const TextFontDefinition& definition, uint8_t bpp) {
    const uint16_t size = getTextFontSize(configuration.defaultSize, definition.id);
    return { getCachedFontFileName(TEXT_CACHE_PREFIX, definition.name, size, bpp, source.version), source.ttf, size, bpp, source.getCodepoints };
}

static FontRequest getMonoRequest(const FontConfiguration& configuration, const FontSource& source, uint8_t bpp) {
    return { getCachedFontFileName(MONO_CACHE_PREFIX, "mono", configuration.defaultSize, bpp, source.version), source.ttf, configuration.defaultSize, bpp, source.getCodepoints };
}

/** Parses a .codepoints file: one "<name> <hex codepoint>" pair per line */
static std::unordered_map<std::string, uint32_t> loadIconCodepoints() {
    std::unordered_map<std::string, uint32_t> codepoints;
    const char* line = reinterpret_cast<const char*>(asset_fonts_MaterialSymbolsRounded_codepoints);
    const char* end = line + asset_fonts_MaterialSymbolsRounded_codepoints_size;
    while (line < end) {
        const char* line_end = static_cast<const char*>(memchr(line, '\n', end - line));
        if (line_end == nullptr) {
            line_end = end;
        }
        const char* separator = static_cast<const char*>(memchr(line, ' ', line_end - line));
        if (separator != nullptr) {
            codepoints[std::string(line, separator - line)] = static_cast<uint32_t>(strtoul(std::string(separator + 1, line_end).c_str(), nullptr, 16));
        }
        line = line_end + 1;
    }
    return codepoints;
}

static FontRequest getIconRequest(const FontConfiguration& configuration, const IconFontDefinition& definition, uint8_t bpp) {
    const uint16_t size = getIconFontSize(configuration.defaultSize, definition.id);
    auto get_codepoints = [&definition] {
        static const auto codepoint_map = loadIconCodepoints();
        std::vector<uint32_t> codepoints;
        codepoints.reserve(definition.iconNameCount);
        for (size_t i = 0; i < definition.iconNameCount; i++) {
            const auto entry = codepoint_map.find(definition.iconNames[i]);
            if (entry != codepoint_map.end()) {
                codepoints.push_back(entry->second);
            } else {
                LOG_W(TAG, "No codepoint for icon %s", definition.iconNames[i]);
            }
        }
        return codepoints;
    };
    return {
        getCachedFontFileName(ICON_CACHE_PREFIX, definition.name, size, bpp, LVGL_ICON_FONT_VERSION),
        { "", asset_fonts_MaterialSymbolsRounded_ttf, asset_fonts_MaterialSymbolsRounded_ttf_size },
        size,
        bpp,
        get_codepoints
    };
}

/** Fonts that aren't part of the firmware are loaded into RAM without external memory */
static void warnWhenInRam(const FontRequest& request) {
    if (memory_external_total() == 0) {
        LOG_W(TAG, "%s is not part of the firmware, so it is or will be in RAM", request.fileName.c_str());
    }
}

static BinFont* load(const FontRequest& request) {
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (findGeneratedFont(request.fileName, data, size)) {
        BinFont* font = nullptr;
        if (binfont_open_memory(data, size, false, &font) == ERROR_NONE) {
            return font;
        }
        LOG_E(TAG, "Failed to open %s from the firmware", request.fileName.c_str());
    }
    warnWhenInRam(request);
    return loadOrGenerateFont(request.fileName, request.ttf, request.size, request.bpp, request.getCodepoints);
}

static bool ensureCached(const FontRequest& request) {
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (findGeneratedFont(request.fileName, data, size)) {
        return true;
    }
    warnWhenInRam(request);
    return ensureCachedFont(request.fileName, request.ttf, request.size, request.bpp, request.getCodepoints);
}

static lv_font_t* createLvglFont(BinFont* font) {
    lv_font_t* lv_font = lvgl_binfont_create(font);
    if (lv_font == nullptr) {
        LOG_E(TAG, "Failed to create LVGL font");
        binfont_close(font);
        return nullptr;
    }
    loadedFonts.push_back({ font, lv_font });
    return lv_font;
}

static void unloadFonts() {
    for (const auto& definition : TEXT_FONTS) {
        lvgl_set_text_font(definition.id, nullptr, 0);
    }
    for (const auto& definition : ICON_FONTS) {
        lvgl_set_icon_font(definition.id, nullptr, 0);
    }
    for (const auto& loaded : loadedFonts) {
        lvgl_binfont_destroy(loaded.lvFont);
        binfont_close(loaded.binFont);
    }
    loadedFonts.clear();
}

bool isFontChangeSupported() {
    constexpr size_t MINIMUM_TOTAL_MEMORY = 2 * 1024 * 1024;
    return memory_heap_total() + memory_external_total() >= MINIMUM_TOTAL_MEMORY;
}

FontConfiguration loadFontConfiguration() {
    if (!isFontChangeSupported()) {
        return { .defaultSize = getDefaultFontSize(), .regularFontPath = "", .monoFontPath = "" };
    }
    const auto settings = settings::appearance::loadOrGetDefault();
    return {
        .defaultSize = settings.fontSize != 0 ? settings.fontSize : getDefaultFontSize(),
        .regularFontPath = settings.regularFontPath,
        .monoFontPath = settings.monoFontPath,
    };
}

FontConfiguration getFontConfiguration() {
    auto& mutex = getConfigurationMutex();
    mutex_lock(&mutex);
    const std::optional<FontConfiguration> active = activeConfiguration;
    mutex_unlock(&mutex);
    return active.has_value() ? *active : loadFontConfiguration();
}

bool updateFontCache(const FontConfiguration& configuration) {
    const uint8_t bpp = getGeneratedFontBpp();

    const auto text_source = getTextSource(configuration);
    for (const auto& definition : TEXT_FONTS) {
        if (isTextFontGenerated(definition.id) && !ensureCached(getTextRequest(configuration, text_source, definition, bpp))) {
            return false;
        }
    }
    for (const auto& definition : ICON_FONTS) {
        if (isIconFontGenerated(definition.id) && !ensureCached(getIconRequest(configuration, definition, bpp))) {
            return false;
        }
    }
    return ensureCached(getMonoRequest(configuration, getMonoSource(configuration), bpp));
}

void loadFonts(const FontConfiguration& configuration) {
    unloadFonts();
    const uint8_t bpp = getGeneratedFontBpp();

    std::vector<std::string> text_file_names;
    const auto text_source = getTextSource(configuration);
    for (const auto& definition : TEXT_FONTS) {
        if (!isTextFontGenerated(definition.id)) {
            continue;
        }
        auto request = getTextRequest(configuration, text_source, definition, bpp);
        BinFont* font = load(request);
        if (font == nullptr && text_source.custom) {
            LOG_W(TAG, "Falling back to the system font for %s text", definition.name);
            request = getTextRequest(configuration, getSystemSource(asset_fonts_AdwaitaSans_ttf, asset_fonts_AdwaitaSans_ttf_size, TT_TEXT_FONT_VERSION), definition, bpp);
            font = load(request);
        }
        if (font == nullptr) {
            continue;
        }
        text_file_names.push_back(request.fileName);
        lv_font_t* lv_font = createLvglFont(font);
        if (lv_font != nullptr) {
            // Glyphs that the font lacks, such as LV_SYMBOL_*, come from LVGL's built-in font
            lv_font->fallback = LV_FONT_DEFAULT;
            lvgl_set_text_font(definition.id, lv_font, request.size);
        }
    }

    if (!isTextFontGenerated(FONT_SIZE_LARGE)) {
        lvgl_set_text_font(FONT_SIZE_LARGE, lvgl_get_text_font(FONT_SIZE_DEFAULT), lvgl_get_text_font_height(FONT_SIZE_DEFAULT));
    }

    std::vector<std::string> icon_file_names;
    for (const auto& definition : ICON_FONTS) {
        if (!isIconFontGenerated(definition.id)) {
            continue;
        }
        const auto request = getIconRequest(configuration, definition, bpp);
        BinFont* font = load(request);
        if (font == nullptr) {
            continue;
        }
        icon_file_names.push_back(request.fileName);
        lv_font_t* lv_font = createLvglFont(font);
        if (lv_font != nullptr) {
            lvgl_set_icon_font(definition.id, lv_font, request.size);
        }
    }

    if (!isIconFontGenerated(LVGL_ICON_FONT_SHARED_LARGE)) {
        lvgl_set_icon_font(LVGL_ICON_FONT_SHARED_LARGE, lvgl_get_shared_icon_default_font(), lvgl_get_shared_icon_default_font_height());
    }

    deleteStaleCachedFonts(TEXT_CACHE_PREFIX, text_file_names);
    deleteStaleCachedFonts(ICON_CACHE_PREFIX, icon_file_names);
    deleteStaleCachedFonts(MONO_CACHE_PREFIX, { getMonoRequest(configuration, getMonoSource(configuration), bpp).fileName });

    auto& mutex = getConfigurationMutex();
    mutex_lock(&mutex);
    activeConfiguration = configuration;
    mutex_unlock(&mutex);
}

BinFont* loadMonoFont() {
    const auto configuration = getFontConfiguration();
    const uint8_t bpp = getGeneratedFontBpp();
    const auto source = getMonoSource(configuration);
    BinFont* font = load(getMonoRequest(configuration, source, bpp));
    if (font == nullptr && source.custom) {
        LOG_W(TAG, "Falling back to the system monospace font");
        font = load(getMonoRequest(configuration, getSystemSource(asset_fonts_AdwaitaMono_ttf, asset_fonts_AdwaitaMono_ttf_size, TT_MONO_FONT_VERSION), bpp));
    }
    return font;
}

}
