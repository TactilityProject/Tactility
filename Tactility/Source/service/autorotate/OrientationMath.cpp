#include <Tactility/service/autorotate/OrientationMath.h>

#include <cmath>

namespace tt::service::autorotate {

constexpr float FLAT_THRESHOLD = 0.45F; // gravity in the screen plane below this = lying flat
constexpr int DEBOUNCE = 5; // consecutive readings before switching

// gx toward +Y -> rotation 0; each +90 degrees of gx/gy angle is one more quarter-turn.
static lv_display_rotation_t gravityToRotation(float gx, float gy, int mountQuarterTurns, lv_display_rotation_t fallback) {
    float plane = sqrtf((gx * gx) + (gy * gy));
    if (plane < FLAT_THRESHOLD) {
        return fallback; // lying flat: keep the current one
    }

    float ang = atan2f(gx, gy) * 180.0F / (float)M_PI; // 0 = grav toward +Y, +90 = grav toward +X
    if (ang < 0.0F) {
        ang += 360.0F;
    }

    int idx = ((int)((ang + 45.0F) / 90.0F) + mountQuarterTurns) % 4;
    static constexpr lv_display_rotation_t map[4] = {
        LV_DISPLAY_ROTATION_0,
        LV_DISPLAY_ROTATION_90,
        LV_DISPLAY_ROTATION_180,
        LV_DISPLAY_ROTATION_270,
    };
    return map[idx];
}

void OrientationMath::reset(lv_display_rotation_t rotation) {
    current = rotation;
    target = rotation;
    stableCount = 0;
}

lv_display_rotation_t OrientationMath::update(float ax, float ay, int mountQuarterTurns) {
    lv_display_rotation_t next = gravityToRotation(ax, ay, mountQuarterTurns, current);
    if (next == current) {
        stableCount = 0;
        return current;
    }

    if (next == target) {
        if (++stableCount >= DEBOUNCE) {
            current = next;
            stableCount = 0;
        }
    } else {
        target = next;
        stableCount = 0;
    }
    return current;
}

} // namespace tt::service::autorotate
