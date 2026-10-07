#include "doctest.h"

#include <lvgl/insets_math.h>

static constexpr DisplayShape RECTANGLE = { .shape = DISPLAY_SHAPE_RECTANGLE, .corner_radius = 0 };
static constexpr DisplayShape CIRCLE = { .shape = DISPLAY_SHAPE_CIRCLE, .corner_radius = 0 };
static constexpr DisplayShape ROUNDED_40 = { .shape = DISPLAY_SHAPE_ROUNDED, .corner_radius = 40 };

TEST_CASE("a rectangle has no insets") {
    const auto insets = lvgl_insets_calculate(RECTANGLE, 320, 240);
    CHECK_EQ(insets.top, 0);
    CHECK_EQ(insets.bottom, 0);
    CHECK_EQ(insets.left, 0);
    CHECK_EQ(insets.right, 0);
    CHECK_EQ(lvgl_insets_calculate_row(RECTANGLE, 320, 240, 0), 0);
}

TEST_CASE("a circle's insets fit the inscribed square") {
    const auto insets = lvgl_insets_calculate(CIRCLE, 240, 240);
    // 120 * (1 - 1/sqrt(2)) = 35.1
    CHECK_EQ(insets.top, 36);
    CHECK_EQ(insets.bottom, 36);
    CHECK_EQ(insets.left, 36);
    CHECK_EQ(insets.right, 36);
}

TEST_CASE("a circle on a wider panel is centered horizontally") {
    const auto insets = lvgl_insets_calculate(CIRCLE, 280, 240);
    CHECK_EQ(insets.top, 36);
    CHECK_EQ(insets.left, 56);
    CHECK_EQ(insets.right, 56);
}

TEST_CASE("a circle's row inset follows its edge") {
    CHECK_EQ(lvgl_insets_calculate_row(CIRCLE, 240, 240, 120), 0);
    CHECK_EQ(lvgl_insets_calculate_row(CIRCLE, 240, 240, 0), 120);
    // Halfway between the center and the top: 120 - sqrt(120^2 - 60^2) = 16.1
    CHECK_EQ(lvgl_insets_calculate_row(CIRCLE, 240, 240, 60), 17);
}

TEST_CASE("a rounded rectangle has insets at the top and bottom only") {
    const auto insets = lvgl_insets_calculate(ROUNDED_40, 172, 320);
    // 40 * (1 - 1/sqrt(2)) = 11.7
    CHECK_EQ(insets.top, 12);
    CHECK_EQ(insets.bottom, 12);
    CHECK_EQ(insets.left, 0);
    CHECK_EQ(insets.right, 0);
}

TEST_CASE("a rounded rectangle's row inset is only in the corners") {
    CHECK_EQ(lvgl_insets_calculate_row(ROUNDED_40, 172, 320, 0), 40);
    CHECK_EQ(lvgl_insets_calculate_row(ROUNDED_40, 172, 320, 319), 40);
    CHECK_EQ(lvgl_insets_calculate_row(ROUNDED_40, 172, 320, 40), 0);
    CHECK_EQ(lvgl_insets_calculate_row(ROUNDED_40, 172, 320, 160), 0);
}

TEST_CASE("a corner radius larger than half the panel is limited") {
    const DisplayShape huge = { .shape = DISPLAY_SHAPE_ROUNDED, .corner_radius = 500 };
    const auto insets = lvgl_insets_calculate(huge, 172, 320);
    // Limited to 86: 86 * (1 - 1/sqrt(2)) = 25.2
    CHECK_EQ(insets.top, 26);
}
