#include "doctest.h"

#include <Tactility/lvgl/Fonts.h>

#include <binfont/binfont.h>
#include <lvgl/fonts.h>

#include <unistd.h>

using namespace tt;

namespace {

/** System font paths are relative to the simulator's working directory, Data/ */
class DataDirectory {
    char previous[512] = {};

public:
    DataDirectory() {
        REQUIRE(getcwd(previous, sizeof(previous)) != nullptr);
        REQUIRE(chdir(TACTILITY_TEST_DATA_DIR) == 0);
    }

    ~DataDirectory() { chdir(previous); }
};

} // namespace

TEST_CASE("loadMonoFont loads the system monospace font with the printable ASCII glyphs") {
    DataDirectory data_directory;
    BinFont* font = lvgl::loadMonoFont();
    REQUIRE(font != nullptr);

    BinFontMetrics metrics;
    binfont_get_metrics(font, &metrics);
    CHECK(metrics.line_height >= 14);
    CHECK(metrics.line_height <= 20);

    BinFontGlyph reference;
    REQUIRE(binfont_get_glyph(font, 'M', &reference));
    for (uint32_t codepoint = 0x20; codepoint <= 0x7E; codepoint++) {
        CAPTURE(codepoint);
        BinFontGlyph glyph;
        REQUIRE(binfont_get_glyph(font, codepoint, &glyph));
        // Monospace: every glyph has the same advance
        CHECK_EQ(glyph.advance_x16, reference.advance_x16);
    }

    BinFontGlyph glyph;
    CHECK_FALSE(binfont_get_glyph(font, 0xE9, &glyph));

    binfont_close(font);
}

TEST_CASE("updateFontCache and loadFonts use a custom regular font and size") {
    DataDirectory data_directory;
    const lvgl::FontConfiguration configuration = {
        .defaultSize = 16,
        .regularFontPath = TACTILITY_TEST_FONTS_DIR "/AdwaitaSans-Regular.ttf",
        .monoFontPath = "",
    };
    CHECK(lvgl::updateFontCache(configuration));

    lvgl::loadFonts(configuration);
    CHECK(lvgl::getFontConfiguration() == configuration);
    CHECK_EQ(lvgl_get_text_font_height(FONT_SIZE_SMALL), 12);
    CHECK_EQ(lvgl_get_text_font_height(FONT_SIZE_DEFAULT), 16);
    CHECK_EQ(lvgl_get_text_font_height(FONT_SIZE_LARGE), 20);
    CHECK(lvgl_get_text_font(FONT_SIZE_DEFAULT) != LV_FONT_DEFAULT);
    CHECK_EQ(lvgl_get_shared_icon_default_font_height(), 18);
    CHECK(lvgl_get_shared_icon_default_font() != LV_FONT_DEFAULT);

    // All characters of a custom TTF are rasterized, not just the ones of the system font
    lv_font_glyph_dsc_t glyph;
    CHECK(lv_font_get_glyph_dsc(lvgl_get_text_font(FONT_SIZE_DEFAULT), &glyph, 0x0416 /* Ж */, 0));
}

TEST_CASE("getTtfCharacterCount counts the characters of a TTF") {
    DataDirectory data_directory;
    CHECK(lvgl::getTtfCharacterCount(TACTILITY_TEST_FONTS_DIR "/AdwaitaSans-Regular.ttf") > lvgl::LARGE_FONT_CHARACTER_COUNT);
    CHECK(lvgl::getTtfCharacterCount("system/fonts/AdwaitaSans.ttf") < lvgl::LARGE_FONT_CHARACTER_COUNT);
    CHECK_EQ(lvgl::getTtfCharacterCount("system/fonts/missing.ttf"), 0);
}

TEST_CASE("updateFontCache fails for a file that is not a TrueType font") {
    DataDirectory data_directory;
    const lvgl::FontConfiguration configuration = {
        .defaultSize = 14,
        .regularFontPath = "system/timezones.csv",
        .monoFontPath = "",
    };
    CHECK_FALSE(lvgl::updateFontCache(configuration));
}
