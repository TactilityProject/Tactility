// SPDX-License-Identifier: Apache-2.0
#include <lvgl/insets_math.h>

#include <algorithm>
#include <cmath>

// The part of a corner's square that a circle with the given radius cuts off along its diagonal
static int32_t corner_inset(int32_t radius) {
    return static_cast<int32_t>(std::ceil(radius * (1.0 - M_SQRT1_2)));
}

// The horizontal distance between a circle's edge and its bounding box at the given distance from the circle's center row
static int32_t circle_row_inset(int32_t radius, int32_t distance_from_center) {
    if (distance_from_center >= radius) {
        return radius;
    }
    const double half_width = std::sqrt(static_cast<double>(radius) * radius - static_cast<double>(distance_from_center) * distance_from_center);
    return static_cast<int32_t>(std::ceil(radius - half_width));
}

LvglInsets lvgl_insets_calculate(const DisplayShape& shape, int32_t width, int32_t height) {
    switch (shape.shape) {
        case DISPLAY_SHAPE_ROUNDED: {
            const int32_t radius = std::min<int32_t>(shape.corner_radius, std::min(width, height) / 2);
            const int32_t inset = corner_inset(radius);
            return { .top = inset, .bottom = inset, .left = 0, .right = 0 };
        }
        case DISPLAY_SHAPE_CIRCLE: {
            const int32_t diameter = std::min(width, height);
            const int32_t inset = corner_inset(diameter / 2);
            const int32_t horizontal = inset + (width - diameter) / 2;
            const int32_t vertical = inset + (height - diameter) / 2;
            return { .top = vertical, .bottom = vertical, .left = horizontal, .right = horizontal };
        }
        default:
            return { .top = 0, .bottom = 0, .left = 0, .right = 0 };
    }
}

int32_t lvgl_insets_calculate_row(const DisplayShape& shape, int32_t width, int32_t height, int32_t y) {
    switch (shape.shape) {
        case DISPLAY_SHAPE_ROUNDED: {
            const int32_t radius = std::min<int32_t>(shape.corner_radius, std::min(width, height) / 2);
            // Distance from the row to the center row of the nearest corner's circle
            const int32_t distance = std::max(radius - y, y - (height - 1 - radius));
            return distance > 0 ? circle_row_inset(radius, distance) : 0;
        }
        case DISPLAY_SHAPE_CIRCLE: {
            const int32_t diameter = std::min(width, height);
            const int32_t radius = diameter / 2;
            const int32_t distance = std::abs(y - height / 2);
            return std::min(circle_row_inset(radius, distance) + (width - diameter) / 2, width / 2);
        }
        default:
            return 0;
    }
}
