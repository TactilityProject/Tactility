#pragma once

#include <cstddef>
#include <cstdint>

namespace tt::app::touchcalibration {

/** The display rotations of LVGL, clockwise */
enum class Rotation {
    None,
    Rotate90,
    Rotate180,
    Rotate270
};

struct Point {
    int32_t x;
    int32_t y;
};

/**
 * Maps a point of the rotated display back to the panel's native orientation, which is
 * where the pointer applies a calibration, before LVGL rotates the points.
 * @param[in] point a point in the coordinates of the rotated display
 * @param[in] rotation the rotation of the display
 * @param[in] nativeWidth the width of the panel without rotation
 * @param[in] nativeHeight the height of the panel without rotation
 */
Point toNativePoint(Point point, Rotation rotation, int32_t nativeWidth, int32_t nativeHeight);

/**
 * Fits the raw values of one axis that were read at known positions, to the raw range that spans the panel.
 * The calibration maps [min, max] onto [0, nativeMax].
 * @param[in] targets the positions that were touched, in native pixels
 * @param[in] samples the raw values that were read at those positions
 * @param[in] count the number of points, at least 2
 * @param[in] nativeMax the last pixel of the axis
 * @return false when the points don't give an increasing range
 */
bool computeRange(const int32_t* targets, const int32_t* samples, size_t count, int32_t nativeMax, int32_t& min, int32_t& max);

} // namespace tt::app::touchcalibration
