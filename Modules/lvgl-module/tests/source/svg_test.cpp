#include "doctest.h"

#include <lvgl.h>
#include <lvgl/svg.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

static std::string writeSvg(const char* name, const char* content) {
    std::string path = std::string("/tmp/") + name;
    FILE* file = fopen(path.c_str(), "w");
    REQUIRE_NE(file, nullptr);
    fputs(content, file);
    fclose(file);
    return path;
}

static lv_color32_t pixelAt(const lv_draw_buf_t* buf, uint32_t x, uint32_t y) {
    return reinterpret_cast<const lv_color32_t*>(buf->data + y * buf->header.stride)[x];
}

TEST_CASE("lvgl_svg_rasterize_file draws a scaled, centered shape") {
    lv_init();
    // A red square on a 10x10 canvas is scaled up to fill the 32x32 buffer
    auto path = writeSvg("tt_svg_test_square.svg",
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
        "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#ff0000\"/></svg>");

    lv_draw_buf_t* buf = lvgl_svg_rasterize_file(path.c_str(), 32);
    REQUIRE_NE(buf, nullptr);
    CHECK_EQ(buf->header.w, 32);
    CHECK_EQ(buf->header.h, 32);
    auto center = pixelAt(buf, 16, 16);
    CHECK_EQ(center.red, 255);
    CHECK_EQ(center.green, 0);
    CHECK_EQ(center.blue, 0);
    CHECK_EQ(center.alpha, 255);
    lv_draw_buf_destroy(buf);
    remove(path.c_str());
    lv_deinit();
}

TEST_CASE("lvgl_svg_rasterize_file keeps the aspect ratio and leaves the rest transparent") {
    lv_init();
    // A 20x10 shape is scaled to 32x16 and centered vertically
    auto path = writeSvg("tt_svg_test_wide.svg",
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\">"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"10\" fill=\"#00ff00\"/></svg>");

    lv_draw_buf_t* buf = lvgl_svg_rasterize_file(path.c_str(), 32);
    REQUIRE_NE(buf, nullptr);
    CHECK_EQ(pixelAt(buf, 16, 16).alpha, 255);
    CHECK_EQ(pixelAt(buf, 16, 2).alpha, 0);
    CHECK_EQ(pixelAt(buf, 16, 29).alpha, 0);
    lv_draw_buf_destroy(buf);
    remove(path.c_str());
    lv_deinit();
}

TEST_CASE("lvgl_svg_rasterize_file fails on missing and invalid files") {
    lv_init();
    CHECK_EQ(lvgl_svg_rasterize_file("/tmp/tt_svg_test_missing.svg", 32), nullptr);
    auto path = writeSvg("tt_svg_test_invalid.svg", "not an svg");
    CHECK_EQ(lvgl_svg_rasterize_file(path.c_str(), 32), nullptr);
    CHECK_EQ(lvgl_svg_rasterize_file(path.c_str(), 0), nullptr);
    remove(path.c_str());
    lv_deinit();
}

static int rasterizeAllIn(const char* directory) {
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".svg") continue;
        for (uint32_t size : { 24u, 48u }) {
            lv_draw_buf_t* buf = lvgl_svg_rasterize_file(entry.path().c_str(), size);
            INFO(entry.path().string());
            REQUIRE_NE(buf, nullptr);
            bool visible = false;
            for (uint32_t y = 0; y < size && !visible; y++) {
                for (uint32_t x = 0; x < size; x++) {
                    if (pixelAt(buf, x, y).alpha > 0) { visible = true; break; }
                }
            }
            CHECK(visible);
            lv_draw_buf_destroy(buf);
        }
        count++;
    }
    return count;
}

TEST_CASE("every app and file icon rasterizes to a visible image") {
    lv_init();
    CHECK_EQ(rasterizeAllIn(APP_ICON_DIR), 58);
    CHECK_EQ(rasterizeAllIn(FILE_ICON_DIR), 18);
    lv_deinit();
}

static void checkEveryIconHasMonoVariant(const char* directory) {
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto stem = entry.path().stem().string();
        if (stem.ends_with("_mono")) continue;
        INFO(entry.path().string());
        CHECK(std::filesystem::exists(entry.path().parent_path() / (stem + "_mono.svg")));
    }
}

TEST_CASE("every colour icon has a mono variant") {
    checkEveryIconHasMonoVariant(APP_ICON_DIR);
    checkEveryIconHasMonoVariant(FILE_ICON_DIR);
}

static const char* SQUARE = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"><rect width=\"10\" height=\"10\" fill=\"#000\"/></svg>";

TEST_CASE("lvgl_svg_rasterize_variants picks the variant of the theme and falls back to the other one") {
    lv_init();
    const size_t length = strlen(SQUARE);
    bool mono = false;

    lv_draw_buf_t* buf = lvgl_svg_rasterize_variants(SQUARE, length, SQUARE, length, true, 16, &mono);
    REQUIRE_NE(buf, nullptr);
    CHECK(mono);
    lv_draw_buf_destroy(buf);

    buf = lvgl_svg_rasterize_variants(SQUARE, length, SQUARE, length, false, 16, &mono);
    REQUIRE_NE(buf, nullptr);
    CHECK_FALSE(mono);
    lv_draw_buf_destroy(buf);

    // The mono variant is missing on a mono theme
    buf = lvgl_svg_rasterize_variants(SQUARE, length, nullptr, 0, true, 16, &mono);
    REQUIRE_NE(buf, nullptr);
    CHECK_FALSE(mono);
    lv_draw_buf_destroy(buf);

    // The colour variant is missing on a colour theme
    buf = lvgl_svg_rasterize_variants(nullptr, 0, SQUARE, length, false, 16, &mono);
    REQUIRE_NE(buf, nullptr);
    CHECK(mono);
    lv_draw_buf_destroy(buf);

    // The preferred variant can't be parsed
    buf = lvgl_svg_rasterize_variants("invalid", 7, SQUARE, length, false, 16, &mono);
    REQUIRE_NE(buf, nullptr);
    CHECK(mono);
    lv_draw_buf_destroy(buf);

    CHECK_EQ(lvgl_svg_rasterize_variants(nullptr, 0, nullptr, 0, true, 16, &mono), nullptr);
    lv_deinit();
}

TEST_CASE("lvgl_svg_rasterize_file_variants falls back when a file is missing") {
    lv_init();
    auto colour = writeSvg("tt_svg_test_variant.svg", SQUARE);
    bool mono = true;

    lv_draw_buf_t* buf = lvgl_svg_rasterize_file_variants(colour.c_str(), "/tmp/tt_svg_test_variant_missing.svg", true, 16, &mono);
    REQUIRE_NE(buf, nullptr);
    CHECK_FALSE(mono);
    lv_draw_buf_destroy(buf);

    buf = lvgl_svg_rasterize_file_variants(nullptr, colour.c_str(), false, 16, &mono);
    REQUIRE_NE(buf, nullptr);
    CHECK(mono);
    lv_draw_buf_destroy(buf);

    CHECK_EQ(lvgl_svg_rasterize_file_variants("/tmp/tt_svg_test_a_missing.svg", "/tmp/tt_svg_test_b_missing.svg", true, 16, &mono), nullptr);
    remove(colour.c_str());
    lv_deinit();
}

TEST_CASE("lvgl_svg_rasterize_data rejects sizes whose pixel buffer would overflow") {
    lv_init();
    const size_t length = strlen(SQUARE);
    CHECK_EQ(lvgl_svg_rasterize_data(SQUARE, length, 0), nullptr);
    CHECK_EQ(lvgl_svg_rasterize_data(SQUARE, length, 32768), nullptr);
    CHECK_EQ(lvgl_svg_rasterize_data(SQUARE, length, UINT32_MAX), nullptr);
    lv_deinit();
}

TEST_CASE("lvgl_svg_round_corners clears the corners and keeps the rest") {
    lv_init();
    auto path = writeSvg("tt_svg_test_round.svg",
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
        "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#ff0000\"/></svg>");

    lv_draw_buf_t* buf = lvgl_svg_rasterize_file(path.c_str(), 32);
    REQUIRE_NE(buf, nullptr);
    lvgl_svg_round_corners(buf, 8);
    CHECK_EQ(pixelAt(buf, 0, 0).alpha, 0);
    CHECK_EQ(pixelAt(buf, 31, 0).alpha, 0);
    CHECK_EQ(pixelAt(buf, 0, 31).alpha, 0);
    CHECK_EQ(pixelAt(buf, 31, 31).alpha, 0);
    CHECK_EQ(pixelAt(buf, 16, 0).alpha, 255);
    CHECK_EQ(pixelAt(buf, 0, 16).alpha, 255);
    CHECK_EQ(pixelAt(buf, 16, 16).alpha, 255);
    CHECK_GT(pixelAt(buf, 2, 2).alpha, 0);
    CHECK_LT(pixelAt(buf, 2, 2).alpha, 255);
    lv_draw_buf_destroy(buf);
    remove(path.c_str());
    lv_deinit();
}

TEST_CASE("lvgl_svg_round_corners with radius 0 changes nothing") {
    lv_init();
    auto path = writeSvg("tt_svg_test_round0.svg",
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
        "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#ff0000\"/></svg>");

    lv_draw_buf_t* buf = lvgl_svg_rasterize_file(path.c_str(), 32);
    REQUIRE_NE(buf, nullptr);
    lvgl_svg_round_corners(buf, 0);
    CHECK_EQ(pixelAt(buf, 0, 0).alpha, 255);
    lv_draw_buf_destroy(buf);
    remove(path.c_str());
    lv_deinit();
}
