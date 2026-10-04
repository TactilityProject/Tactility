#include "doctest.h"

#include <Tactility/lvgl/FontCache.h>

#include <binfont/binfont.h>

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

TEST_CASE("loadMonoFont loads a monospace font with the printable ASCII glyphs") {
    DataDirectory data_directory;
    BinFont* font = lvgl::loadMonoFont(14);
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
