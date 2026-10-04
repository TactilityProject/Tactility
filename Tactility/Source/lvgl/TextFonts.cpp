#include <Tactility/lvgl/TextFonts.h>

#include <Tactility/lvgl/FontCache.h>
#include <Tactility/lvgl/FontSizes.h>
#include <Tactility/lvgl/FontVersions.h>

#include <binfont/binfont.h>
#include <lvgl/binfont.h>
#include <lvgl/fonts.h>
#include <tactility/log.h>

#include <string>
#include <vector>

namespace tt::lvgl {

constexpr auto* TAG = "TextFonts";
constexpr auto* CACHE_FILE_PREFIX = "adwaita_sans_";

struct TextFontDefinition {
    LvglFontSize id;
    const char* name;
};

void initTextFonts() {
    const TextFontDefinition definitions[] = {
        { FONT_SIZE_SMALL, "small" },
        { FONT_SIZE_DEFAULT, "default" },
        { FONT_SIZE_LARGE, "large" },
    };

    const uint8_t bpp = getGeneratedFontBpp();
    std::vector<std::string> file_names;
    for (const auto& definition : definitions) {
        file_names.push_back(getCachedFontFileName(CACHE_FILE_PREFIX, definition.name, getTextFontSize(definition.id), bpp, TT_TEXT_FONT_VERSION));
    }
    deleteStaleCachedFonts(CACHE_FILE_PREFIX, file_names);

    const auto ttf_path = getSystemFontPath("AdwaitaSans.ttf");
    for (size_t index = 0; index < std::size(definitions); index++) {
        const auto& definition = definitions[index];
        const uint16_t size = getTextFontSize(definition.id);

        // The TTF is a subset with only the supported characters, so all of its codepoints are used
        BinFont* font = loadOrGenerateFont(file_names[index], ttf_path, size, bpp, [] { return std::vector<uint32_t>(); });
        if (font == nullptr) {
            continue;
        }

        lv_font_t* lv_font = lvgl_binfont_create(font);
        if (lv_font == nullptr) {
            LOG_E(TAG, "Failed to create LVGL font for %s", definition.name);
            binfont_close(font);
            continue;
        }
        // Glyphs that the TTF subset lacks, such as LV_SYMBOL_*, come from LVGL's built-in font
        lv_font->fallback = LV_FONT_DEFAULT;
        lvgl_set_text_font(definition.id, lv_font, size);
    }
}

}
