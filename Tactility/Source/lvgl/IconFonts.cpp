#include <Tactility/lvgl/IconFonts.h>

#include <Tactility/file/File.h>
#include <Tactility/lvgl/FontCache.h>
#include <Tactility/lvgl/FontSizes.h>

#include <binfont/binfont.h>
#include <lvgl/binfont.h>
#include <lvgl/fonts.h>
#include <lvgl/icons/names.h>
#include <tactility/log.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace tt::lvgl {

constexpr auto* TAG = "IconFonts";
constexpr auto* CACHE_FILE_PREFIX = "material_symbols_";

struct IconFontDefinition {
    LvglIconFont id;
    const char* name;
    const char* const* iconNames;
    size_t iconNameCount;
};

/** Parses a .codepoints file: one "<name> <hex codepoint>" pair per line */
static std::unordered_map<std::string, uint32_t> loadCodepoints(const std::string& path) {
    std::unordered_map<std::string, uint32_t> codepoints;
    file::readLines(path, true, [&codepoints](const char* line) {
        const char* separator = strchr(line, ' ');
        if (separator != nullptr) {
            codepoints[std::string(line, separator - line)] = static_cast<uint32_t>(strtoul(separator + 1, nullptr, 16));
        }
    });
    return codepoints;
}

void initIconFonts() {
    const IconFontDefinition definitions[] = {
        { LVGL_ICON_FONT_STATUSBAR, "statusbar", lvgl_icon_statusbar_names, lvgl_icon_statusbar_name_count },
        { LVGL_ICON_FONT_LAUNCHER, "launcher", lvgl_icon_launcher_names, lvgl_icon_launcher_name_count },
        { LVGL_ICON_FONT_SHARED, "shared", lvgl_icon_shared_names, lvgl_icon_shared_name_count },
        { LVGL_ICON_FONT_SHARED_2X, "shared2x", lvgl_icon_shared_names, lvgl_icon_shared_name_count },
    };

    const uint8_t bpp = getGeneratedFontBpp();
    std::vector<std::string> file_names;
    for (const auto& definition : definitions) {
        file_names.push_back(getCachedFontFileName(CACHE_FILE_PREFIX, definition.name, getIconFontSize(definition.id), bpp, LVGL_ICON_FONT_VERSION));
    }
    deleteStaleCachedFonts(CACHE_FILE_PREFIX, file_names);

    const auto ttf_path = getSystemFontPath("MaterialSymbolsRounded.ttf");
    std::unordered_map<std::string, uint32_t> codepoint_map;
    for (size_t index = 0; index < std::size(definitions); index++) {
        const auto& definition = definitions[index];
        const uint16_t size = getIconFontSize(definition.id);

        BinFont* font = loadOrGenerateFont(file_names[index], ttf_path, size, bpp, [&] {
            if (codepoint_map.empty()) {
                codepoint_map = loadCodepoints(getSystemFontPath("MaterialSymbolsRounded.codepoints"));
            }
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
        });
        if (font == nullptr) {
            continue;
        }

        lv_font_t* lv_font = lvgl_binfont_create(font);
        if (lv_font == nullptr) {
            LOG_E(TAG, "Failed to create LVGL font for %s", definition.name);
            binfont_close(font);
            continue;
        }
        lvgl_set_icon_font(definition.id, lv_font, size);
    }
}

}
