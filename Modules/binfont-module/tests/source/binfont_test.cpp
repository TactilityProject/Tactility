#include "doctest.h"

#include <binfont/binfont.h>
#include <binfont_private.h>
#include <binfont/generator.h>
#include <binfont/render.h>
#include <graphics/pixel_buffer.h>
#include <tactility/memory.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr uint32_t ICON_CODEPOINTS[] = { 0xE5C3, 0xE8B8, 0xE2C7, 0xE63E, 0xE1A7 };

std::string fixture(const char* name) {
    return std::string(BINFONT_TEST_FIXTURES_DIR) + "/" + name;
}

/** Loads a file into memory allocated with memory_alloc() */
uint8_t* load_file(const std::string& path, size_t& size) {
    FILE* file = fopen(path.c_str(), "rb");
    REQUIRE(file != nullptr);
    fseek(file, 0, SEEK_END);
    size = static_cast<size_t>(ftell(file));
    fseek(file, 0, SEEK_SET);
    auto* data = static_cast<uint8_t*>(memory_alloc(size));
    REQUIRE(fread(data, 1, size, file) == size);
    fclose(file);
    return data;
}

BinFont* open_fixture_in_memory(const char* name) {
    size_t size;
    uint8_t* data = load_file(fixture(name), size);
    BinFont* font = nullptr;
    REQUIRE(binfont_open_memory(data, size, true, &font) == ERROR_NONE);
    return font;
}

std::vector<uint8_t> decode(BinFont* font, const BinFontGlyph& glyph) {
    std::vector<uint8_t> bitmap(static_cast<size_t>(glyph.width) * glyph.height);
    REQUIRE(binfont_get_glyph_bitmap(font, &glyph, bitmap.data(), glyph.width) == ERROR_NONE);
    return bitmap;
}

uint32_t sum(const std::vector<uint8_t>& bitmap) {
    uint32_t total = 0;
    for (uint8_t value : bitmap) {
        total += value;
    }
    return total;
}

void check_same_glyph(const BinFontGlyph& a, const BinFontGlyph& b) {
    CHECK_EQ(a.glyph_id, b.glyph_id);
    CHECK_EQ(a.advance_x16, b.advance_x16);
    CHECK_EQ(a.x, b.x);
    CHECK_EQ(a.y, b.y);
    CHECK_EQ(a.width, b.width);
    CHECK_EQ(a.height, b.height);
}

} // namespace

TEST_CASE("binfont_open_file returns ERROR_NOT_FOUND for a missing file") {
    BinFont* font = nullptr;
    CHECK_EQ(binfont_open_file(fixture("missing.bin").c_str(), &font), ERROR_NOT_FOUND);
}

TEST_CASE("binfont_open_memory rejects data that is not a font") {
    const uint8_t data[64] = { 1, 2, 3, 4 };
    BinFont* font = nullptr;
    CHECK_EQ(binfont_open_memory(data, sizeof(data), false, &font), ERROR_NOT_SUPPORTED);
}

TEST_CASE("binfont reads metrics, glyphs and bitmaps that match lv_font_conv's C output") {
    BinFont* font = open_fixture_in_memory("text_kern_pairs.bin");

    BinFontMetrics metrics;
    binfont_get_metrics(font, &metrics);
    CHECK_EQ(metrics.size, 20);
    CHECK_EQ(metrics.line_height, 20);
    CHECK_EQ(metrics.base_line, 5);
    CHECK_EQ(metrics.ascent, 15);
    CHECK_EQ(metrics.descent, -5);
    CHECK_EQ(metrics.underline_position, 1);
    CHECK_EQ(metrics.underline_thickness, 0);
    CHECK_EQ(metrics.bpp, 4);

    BinFontGlyph glyph;
    REQUIRE(binfont_get_glyph(font, 'A', &glyph));
    CHECK_EQ(glyph.glyph_id, 1);
    CHECK_EQ(glyph.advance_x16, 195);
    CHECK_EQ(glyph.x, 0);
    CHECK_EQ(glyph.y, 0);
    CHECK_EQ(glyph.width, 13);
    CHECK_EQ(glyph.height, 14);

    const auto bitmap = decode(font, glyph);
    const std::vector<uint8_t> first_row = { 0, 0, 0, 0, 0, 238, 255, 17, 0, 0, 0, 0, 0 };
    const std::vector<uint8_t> last_row = { 221, 255, 0, 0, 0, 0, 0, 0, 0, 0, 238, 238, 0 };
    CHECK(std::vector<uint8_t>(bitmap.begin(), bitmap.begin() + 13) == first_row);
    CHECK(std::vector<uint8_t>(bitmap.end() - 13, bitmap.end()) == last_row);
    CHECK_EQ(sum(bitmap), 13974);

    REQUIRE(binfont_get_glyph(font, 'V', &glyph));
    CHECK_EQ(glyph.glyph_id, 22);
    REQUIRE(binfont_get_glyph(font, 'z', &glyph));
    CHECK_EQ(glyph.glyph_id, 52);

    CHECK_FALSE(binfont_get_glyph(font, '0', &glyph));
    CHECK_FALSE(binfont_get_glyph(font, 0x5B, &glyph));
    CHECK_FALSE(binfont_get_glyph(font, 0x10FFFF, &glyph));

    binfont_close(font);
}

TEST_CASE("binfont kerning is the same for the pairs and the classes format") {
    BinFont* pairs = open_fixture_in_memory("text_kern_pairs.bin");
    BinFont* classes = open_fixture_in_memory("text_kern_classes.bin");

    CHECK_EQ(binfont_get_kerning_x16(pairs, 1, 22), -10);
    CHECK_EQ(binfont_get_kerning_x16(classes, 1, 22), -10);

    int non_zero = 0;
    for (uint32_t left = 1; left <= 52; left++) {
        for (uint32_t right = 1; right <= 52; right++) {
            const int32_t value = binfont_get_kerning_x16(pairs, left, right);
            CHECK_EQ(value, binfont_get_kerning_x16(classes, left, right));
            if (value != 0) {
                non_zero++;
            }
        }
    }
    CHECK(non_zero > 0);
    CHECK_EQ(binfont_get_kerning_x16(pairs, 1, 1000), 0);
    CHECK_EQ(binfont_get_kerning_x16(classes, 1, 1000), 0);

    binfont_close(pairs);
    binfont_close(classes);
}

TEST_CASE("binfont decodes identical bitmaps for raw, RLE and RLE with prefilter") {
    for (const char* bpp : { "2", "4" }) {
        CAPTURE(bpp);
        BinFont* raw = open_fixture_in_memory((std::string("icons_raw_") + bpp + ".bin").c_str());
        BinFont* rle = open_fixture_in_memory((std::string("icons_rle_") + bpp + ".bin").c_str());
        BinFont* prefiltered = open_fixture_in_memory((std::string("icons_rlepf_") + bpp + ".bin").c_str());

        for (uint32_t codepoint : ICON_CODEPOINTS) {
            CAPTURE(codepoint);
            BinFontGlyph raw_glyph, rle_glyph, prefiltered_glyph;
            REQUIRE(binfont_get_glyph(raw, codepoint, &raw_glyph));
            REQUIRE(binfont_get_glyph(rle, codepoint, &rle_glyph));
            REQUIRE(binfont_get_glyph(prefiltered, codepoint, &prefiltered_glyph));
            check_same_glyph(raw_glyph, rle_glyph);
            check_same_glyph(raw_glyph, prefiltered_glyph);

            const auto raw_bitmap = decode(raw, raw_glyph);
            CHECK(sum(raw_bitmap) > 0);
            CHECK(decode(rle, rle_glyph) == raw_bitmap);
            CHECK(decode(prefiltered, prefiltered_glyph) == raw_bitmap);
        }

        binfont_close(raw);
        binfont_close(rle);
        binfont_close(prefiltered);
    }
}

TEST_CASE("binfont gives the same results when streaming from a file") {
    BinFont* memory_font = open_fixture_in_memory("icons_rlepf_4.bin");
    BinFont* file_font = nullptr;
    REQUIRE(binfont_open_file_streaming(fixture("icons_rlepf_4.bin").c_str(), &file_font) == ERROR_NONE);

    for (uint32_t codepoint : ICON_CODEPOINTS) {
        BinFontGlyph memory_glyph, file_glyph;
        REQUIRE(binfont_get_glyph(memory_font, codepoint, &memory_glyph));
        REQUIRE(binfont_get_glyph(file_font, codepoint, &file_glyph));
        check_same_glyph(memory_glyph, file_glyph);
        CHECK(decode(memory_font, memory_glyph) == decode(file_font, file_glyph));
    }

    binfont_close(memory_font);
    binfont_close(file_font);
}

TEST_CASE("binfont_open_file loads the font into memory") {
    const std::string path = std::string(BINFONT_TEST_TEMP_DIR) + "/binfont_memory_test.bin";
    size_t size;
    uint8_t* data = load_file(fixture("icons_rlepf_4.bin"), size);
    FILE* copy = fopen(path.c_str(), "wb");
    REQUIRE(copy != nullptr);
    REQUIRE(fwrite(data, 1, size, copy) == size);
    fclose(copy);
    memory_free(data);

    BinFont* font = nullptr;
    REQUIRE(binfont_open_file(path.c_str(), &font) == ERROR_NONE);
    REQUIRE(remove(path.c_str()) == 0);

    BinFontGlyph glyph;
    CHECK(binfont_get_glyph(font, ICON_CODEPOINTS[0], &glyph));
    CHECK(sum(decode(font, glyph)) > 0);

    binfont_close(font);
}

TEST_CASE("binfont only keeps a streamed font file open during a session") {
    // An open file stays readable after it's deleted, so deleting it shows whether it's kept open
    const std::string path = std::string(BINFONT_TEST_TEMP_DIR) + "/binfont_session_test.bin";
    size_t size;
    uint8_t* data = load_file(fixture("icons_rlepf_4.bin"), size);
    FILE* copy = fopen(path.c_str(), "wb");
    REQUIRE(copy != nullptr);
    REQUIRE(fwrite(data, 1, size, copy) == size);
    fclose(copy);
    memory_free(data);

    BinFont* font = nullptr;
    REQUIRE(binfont_open_file_streaming(path.c_str(), &font) == ERROR_NONE);
    BinFontGlyph glyph;
    REQUIRE(binfont_get_glyph(font, ICON_CODEPOINTS[0], &glyph));

    REQUIRE(binfont_begin(font) == ERROR_NONE);
    REQUIRE(binfont_begin(font) == ERROR_NONE);
    REQUIRE(remove(path.c_str()) == 0);
    binfont_end(font);
    // Still open: one session remains
    CHECK(binfont_get_glyph(font, ICON_CODEPOINTS[1], &glyph));
    CHECK_FALSE(decode(font, glyph).empty());
    binfont_end(font);

    // Closed: the deleted file can't be opened again
    CHECK_FALSE(binfont_get_glyph(font, ICON_CODEPOINTS[2], &glyph));
    CHECK_EQ(binfont_begin(font), ERROR_NOT_FOUND);

    binfont_close(font);
}

TEST_CASE("binfont_get_glyph_bitmap rejects a stride smaller than the glyph width") {
    BinFont* font = open_fixture_in_memory("icons_raw_2.bin");
    BinFontGlyph glyph;
    REQUIRE(binfont_get_glyph(font, ICON_CODEPOINTS[0], &glyph));
    std::vector<uint8_t> bitmap(static_cast<size_t>(glyph.width) * glyph.height);
    CHECK_EQ(binfont_get_glyph_bitmap(font, &glyph, bitmap.data(), glyph.width - 1), ERROR_INVALID_ARGUMENT);
    binfont_close(font);
}

TEST_CASE("binfont_generate rasterizes a TTF close to lv_font_conv") {
    uint32_t codepoints[] = { 0xE8B8, 0xE5C3, 0x41, 0xE1A7, 0xE2C7, 0xE63E, 0xE5C3 }; // unsorted, duplicate and missing entries
    const BinFontGeneratorConfig config = {
        .ttf_path = BINFONT_TEST_ICON_TTF,
        .size = 24,
        .bpp = 4,
        .codepoints = codepoints,
        .codepoint_count = sizeof(codepoints) / sizeof(codepoints[0]),
    };
    uint8_t* data = nullptr;
    size_t size = 0;
    REQUIRE(binfont_generate(&config, &data, &size) == ERROR_NONE);

    BinFont* generated = nullptr;
    REQUIRE(binfont_open_memory(data, size, true, &generated) == ERROR_NONE);
    BinFont* reference = open_fixture_in_memory("icons_raw_4.bin");

    BinFontMetrics generated_metrics, reference_metrics;
    binfont_get_metrics(generated, &generated_metrics);
    binfont_get_metrics(reference, &reference_metrics);
    CHECK_EQ(generated_metrics.size, 24);
    CHECK_EQ(generated_metrics.bpp, 4);
    CHECK_EQ(generated_metrics.ascent, reference_metrics.ascent);
    CHECK_EQ(generated_metrics.descent, reference_metrics.descent);

    BinFontGlyph glyph;
    CHECK_FALSE(binfont_get_glyph(generated, 0x41, &glyph));

    for (uint32_t codepoint : ICON_CODEPOINTS) {
        CAPTURE(codepoint);
        BinFontGlyph generated_glyph, reference_glyph;
        REQUIRE(binfont_get_glyph(generated, codepoint, &generated_glyph));
        REQUIRE(binfont_get_glyph(reference, codepoint, &reference_glyph));
        CHECK(std::abs(static_cast<int>(generated_glyph.advance_x16) - static_cast<int>(reference_glyph.advance_x16)) <= 16);
        CHECK(std::abs(generated_glyph.x - reference_glyph.x) <= 1);
        CHECK(std::abs(generated_glyph.y - reference_glyph.y) <= 1);
        CHECK(std::abs(generated_glyph.width - reference_glyph.width) <= 1);
        CHECK(std::abs(generated_glyph.height - reference_glyph.height) <= 1);

        const auto generated_sum = static_cast<double>(sum(decode(generated, generated_glyph)));
        const auto reference_sum = static_cast<double>(sum(decode(reference, reference_glyph)));
        CHECK(generated_sum > reference_sum * 0.9);
        CHECK(generated_sum < reference_sum * 1.1);
    }

    binfont_close(generated);
    binfont_close(reference);
}

TEST_CASE("binfont_generate includes every codepoint of the TTF without a codepoint list") {
    const BinFontGeneratorConfig config = {
        .ttf_path = BINFONT_TEST_TEXT_TTF,
        .size = 14,
        .bpp = 2,
        .codepoints = nullptr,
        .codepoint_count = 0,
    };
    uint8_t* data = nullptr;
    size_t size = 0;
    REQUIRE(binfont_generate(&config, &data, &size) == ERROR_NONE);
    BinFont* font = nullptr;
    REQUIRE(binfont_open_memory(data, size, true, &font) == ERROR_NONE);

    BinFontGlyph glyph;
    for (uint32_t codepoint : { 0x20u, 0x41u, 0x7Eu, 0xE9u, 0xFFu, 0x2026u, 0x20ACu }) {
        CAPTURE(codepoint);
        CHECK(binfont_get_glyph(font, codepoint, &glyph));
    }
    // Not in the subset
    CHECK_FALSE(binfont_get_glyph(font, 0x0100, &glyph));
    CHECK_FALSE(binfont_get_glyph(font, 0x0416, &glyph));

    binfont_close(font);
}

TEST_CASE("binfont_generate rejects a codepoint count without codepoints") {
    const BinFontGeneratorConfig config = {
        .ttf_path = BINFONT_TEST_TEXT_TTF,
        .size = 14,
        .bpp = 2,
        .codepoints = nullptr,
        .codepoint_count = 5,
    };
    uint8_t* data = nullptr;
    size_t size = 0;
    CHECK_EQ(binfont_generate(&config, &data, &size), ERROR_INVALID_ARGUMENT);
}

TEST_CASE("binfont_generate fails for a missing TTF") {
    const uint32_t codepoint = 0xE5C3;
    const BinFontGeneratorConfig config = {
        .ttf_path = BINFONT_TEST_FIXTURES_DIR "/missing.ttf",
        .size = 24,
        .bpp = 2,
        .codepoints = &codepoint,
        .codepoint_count = 1,
    };
    uint8_t* data = nullptr;
    size_t size = 0;
    CHECK_EQ(binfont_generate(&config, &data, &size), ERROR_NOT_FOUND);
}

TEST_CASE("binfont draws glyphs and text into a PixelBuffer") {
    BinFont* font = open_fixture_in_memory("text_kern_pairs.bin");
    PixelBuffer* buffer = pixel_buffer_create(DISPLAY_COLOR_FORMAT_RGB565, 64, 24);
    REQUIRE(buffer != nullptr);
    pixel_buffer_clear(buffer);

    const int advance = binfont_draw_glyph_rgb565(buffer, 0, 18, font, 'A', 0xFFFF, PIXEL_BUFFER_CONVERSION_EXACT_BLACK);
    CHECK_EQ(advance, (195 + 8) >> 4);
    // Top of 'A' (row 0 of the glyph, column 6 is fully opaque) sits 14 rows above the baseline
    CHECK_EQ(pixel_buffer_get_pixel_rgb565(buffer, 6, 18 - 14), 0xFFFF);
    CHECK_EQ(pixel_buffer_get_pixel_rgb565(buffer, 0, 0), 0x0000);

    CHECK_EQ(binfont_draw_glyph_rgb565(buffer, 0, 18, font, '0', 0xFFFF, PIXEL_BUFFER_CONVERSION_EXACT_BLACK), 0);

    // "AV" is kerned: 195 - 10 + V's advance
    BinFontGlyph v;
    REQUIRE(binfont_get_glyph(font, 'V', &v));
    pixel_buffer_clear(buffer);
    const int width = binfont_draw_text_rgb565(buffer, 0, 18, font, "AV", 0xFFFF, PIXEL_BUFFER_CONVERSION_EXACT_BLACK);
    CHECK_EQ(width, static_cast<int>((195 - 10 + v.advance_x16 + 8) >> 4));

    pixel_buffer_free(buffer);
    binfont_close(font);
}
