#include "doctest.h"

#include <lvgl/theme.h>

TEST_CASE("lvgl_theme_lerp returns the compact value at 0 and the regular value at 100") {
    CHECK_EQ(lvgl_theme_lerp(2, 16, 0), 2);
    CHECK_EQ(lvgl_theme_lerp(2, 16, 100), 16);
    CHECK_EQ(lvgl_theme_lerp(2, 16, 50), 9);
}

TEST_CASE("lvgl_theme_lerp interpolates in steps of 20 percent") {
    CHECK_EQ(lvgl_theme_lerp(0, 100, 20), 20);
    CHECK_EQ(lvgl_theme_lerp(0, 100, 40), 40);
    CHECK_EQ(lvgl_theme_lerp(0, 100, 60), 60);
    CHECK_EQ(lvgl_theme_lerp(0, 100, 80), 80);
}

TEST_CASE("lvgl_theme_lerp interpolates downwards and with negative values") {
    CHECK_EQ(lvgl_theme_lerp(16, 2, 50), 9);
    CHECK_EQ(lvgl_theme_lerp(-3, -8, 0), -3);
    CHECK_EQ(lvgl_theme_lerp(-3, -8, 100), -8);
}
