#include "doctest.h"

#include <lodepng/lodepng.h>

#include <cstring>

TEST_CASE("lodepng decodes a PNG file") {
    unsigned char* image = nullptr;
    unsigned width = 0;
    unsigned height = 0;
    REQUIRE_EQ(lodepng_decode32_file(&image, &width, &height, LODEPNG_TEST_LOGO), 0);
    CHECK_EQ(width, 80);
    CHECK_EQ(height, 103);
    lodepng_free(image);
}

TEST_CASE("lodepng returns an error for a missing file") {
    unsigned char* image = nullptr;
    unsigned width = 0;
    unsigned height = 0;
    CHECK_NE(lodepng_decode32_file(&image, &width, &height, "missing.png"), 0);
    lodepng_free(image);
}

TEST_CASE("lodepng encodes and decodes an image without changes") {
    constexpr unsigned width = 3;
    constexpr unsigned height = 2;
    const unsigned char pixels[width * height * 4] = {
        255, 0, 0, 255,  0, 255, 0, 255,  0, 0, 255, 255,
        0, 0, 0, 0,      255, 255, 255, 128,  10, 20, 30, 40,
    };

    unsigned char* png = nullptr;
    size_t png_size = 0;
    REQUIRE_EQ(lodepng_encode32(&png, &png_size, pixels, width, height), 0);

    unsigned char* decoded = nullptr;
    unsigned decoded_width = 0;
    unsigned decoded_height = 0;
    REQUIRE_EQ(lodepng_decode32(&decoded, &decoded_width, &decoded_height, png, png_size), 0);
    CHECK_EQ(decoded_width, width);
    CHECK_EQ(decoded_height, height);
    CHECK_EQ(memcmp(decoded, pixels, sizeof(pixels)), 0);

    lodepng_free(png);
    lodepng_free(decoded);
}
