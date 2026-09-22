#pragma once

#include <src/display/lv_display.h>

namespace tt::service::autorotate {

/**
 * Debounced gravity-vector-to-screen-rotation classifier.
 * Works purely in \c lv_display_rotation_t (physical quarter-turns), since that's panel-shape-agnostic;
 * converting to/from settings::display::Orientation is the caller's job (see
 * settings::display::fromLvglDisplayRotation()), because that conversion depends on whether the
 * panel is natively landscape or portrait, which this class has no business knowing about.
 */
class OrientationMath {
    lv_display_rotation_t current = LV_DISPLAY_ROTATION_0;
    lv_display_rotation_t target = LV_DISPLAY_ROTATION_0;
    int stableCount = 0;

public:
    void reset(lv_display_rotation_t rotation);

    /**
     * @param ax, ay gravity-plane components from the IMU, in g
     * @param mountQuarterTurns how the IMU is mounted relative to the panel's upright direction,
     *        as a count of extra 90-degree steps (0-3)
     * @return the debounced current rotation (unchanged until a new reading is stable for
     *         several consecutive updates)
     */
    lv_display_rotation_t update(float ax, float ay, int mountQuarterTurns);
};

} // namespace tt::service::autorotate
