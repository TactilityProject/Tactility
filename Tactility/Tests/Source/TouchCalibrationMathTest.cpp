#include "doctest.h"

#include <Tactility/app/touchcalibration/CalibrationMath.h>

#include <initializer_list>

using namespace tt::app::touchcalibration;

// The point transform of LVGL (lv_display_rotate_point) for a panel of width x height without rotation
static Point rotateLikeLvgl(Point point, Rotation rotation, int32_t width, int32_t height) {
    switch (rotation) {
        case Rotation::Rotate90:
            return { .x = height - point.y - 1, .y = point.x };
        case Rotation::Rotate180:
            return { .x = width - point.x - 1, .y = height - point.y - 1 };
        case Rotation::Rotate270:
            return { .x = point.y, .y = width - point.x - 1 };
        default:
            return point;
    }
}

TEST_CASE("toNativePoint undoes the rotation of the display") {
    constexpr int32_t width = 320;
    constexpr int32_t height = 480;
    for (auto rotation : { Rotation::None, Rotation::Rotate90, Rotation::Rotate180, Rotation::Rotate270 }) {
        for (auto native : { Point { 0, 0 }, Point { 319, 0 }, Point { 0, 479 }, Point { 319, 479 }, Point { 100, 250 } }) {
            const auto rotated = rotateLikeLvgl(native, rotation, width, height);
            const auto restored = toNativePoint(rotated, rotation, width, height);
            CHECK_EQ(restored.x, native.x);
            CHECK_EQ(restored.y, native.y);
        }
    }
}

TEST_CASE("computeRange recovers the range of a sensor with an offset and a scale") {
    // Raw = 100 + 2 * position, so the panel from 0 to 319 spans 100 to 738
    const int32_t targets[] = { 24, 296, 296, 24 };
    int32_t samples[4];
    for (int i = 0; i < 4; i++) {
        samples[i] = 100 + 2 * targets[i];
    }
    int32_t min = 0;
    int32_t max = 0;
    REQUIRE(computeRange(targets, samples, 4, 319, min, max));
    CHECK_EQ(min, 100);
    CHECK_EQ(max, 738);
}

TEST_CASE("computeRange gives the same range when the targets are in another order of the axes") {
    // The first two targets are on the left and the other two on the right, or the other way around
    const int32_t targets[] = { 296, 24, 24, 296 };
    int32_t samples[4];
    for (int i = 0; i < 4; i++) {
        samples[i] = 50 + 3 * targets[i];
    }
    int32_t min = 0;
    int32_t max = 0;
    REQUIRE(computeRange(targets, samples, 4, 319, min, max));
    CHECK_EQ(min, 50);
    CHECK_EQ(max, 50 + 3 * 319);
}

TEST_CASE("computeRange rejects an inverted axis and identical positions") {
    int32_t min = 0;
    int32_t max = 0;
    const int32_t targets[] = { 24, 296 };
    const int32_t inverted[] = { 900, 100 };
    CHECK_FALSE(computeRange(targets, inverted, 2, 319, min, max));
    const int32_t samePosition[] = { 24, 24 };
    const int32_t samples[] = { 100, 120 };
    CHECK_FALSE(computeRange(samePosition, samples, 2, 319, min, max));
    CHECK_FALSE(computeRange(targets, samples, 1, 319, min, max));
}

TEST_CASE("a calibration taken on a rotated display is the same as one on the native display") {
    constexpr int32_t width = 320;
    constexpr int32_t height = 480;
    // The sensor: raw x = 40 + 2.5 * x, raw y = 25 + 1.8 * y, in native pixels
    auto sense = [](Point native) { return Point { 40 + native.x * 5 / 2, 25 + native.y * 9 / 5 }; };

    int32_t expectedXMin = 0, expectedXMax = 0, expectedYMin = 0, expectedYMax = 0;
    {
        const Point natives[] = { { 24, 24 }, { 296, 24 }, { 296, 456 }, { 24, 456 } };
        int32_t tx[4], ty[4], sx[4], sy[4];
        for (int i = 0; i < 4; i++) {
            tx[i] = natives[i].x; ty[i] = natives[i].y;
            sx[i] = sense(natives[i]).x; sy[i] = sense(natives[i]).y;
        }
        REQUIRE(computeRange(tx, sx, 4, width - 1, expectedXMin, expectedXMax));
        REQUIRE(computeRange(ty, sy, 4, height - 1, expectedYMin, expectedYMax));
    }

    for (auto rotation : { Rotation::Rotate90, Rotation::Rotate180, Rotation::Rotate270 }) {
        // The targets are drawn in the rotated display, and the raw sensor values are rotated by LVGL like any point
        const Point nativeTargets[] = { { 24, 24 }, { 296, 24 }, { 296, 456 }, { 24, 456 } };
        int32_t tx[4], ty[4], sx[4], sy[4];
        for (int i = 0; i < 4; i++) {
            const auto shownTarget = rotateLikeLvgl(nativeTargets[i], rotation, width, height);
            const auto shownSample = rotateLikeLvgl(sense(nativeTargets[i]), rotation, width, height);
            // The raw values exceed the display, so they are rotated with the same formula
            const auto target = toNativePoint(shownTarget, rotation, width, height);
            const auto sample = toNativePoint(shownSample, rotation, width, height);
            tx[i] = target.x; ty[i] = target.y; sx[i] = sample.x; sy[i] = sample.y;
        }
        int32_t xMin = 0, xMax = 0, yMin = 0, yMax = 0;
        REQUIRE(computeRange(tx, sx, 4, width - 1, xMin, xMax));
        REQUIRE(computeRange(ty, sy, 4, height - 1, yMin, yMax));
        CHECK_EQ(xMin, expectedXMin);
        CHECK_EQ(xMax, expectedXMax);
        CHECK_EQ(yMin, expectedYMin);
        CHECK_EQ(yMax, expectedYMax);
    }
}
