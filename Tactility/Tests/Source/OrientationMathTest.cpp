#include "doctest.h"
#include <Tactility/service/autorotate/OrientationMath.h>

using tt::service::autorotate::OrientationMath;

TEST_CASE("a flat reading below the threshold keeps the current rotation") {
    OrientationMath math;
    math.reset(LV_DISPLAY_ROTATION_0);
    CHECK_EQ(math.update(0.1F, 0.1F, 0), LV_DISPLAY_ROTATION_0);
}

TEST_CASE("gravity toward +X classifies as rotation 90 when no debounce is needed yet") {
    OrientationMath math;
    math.reset(LV_DISPLAY_ROTATION_0);
    // A single reading only sets the debounce target - the previous rotation is still returned.
    CHECK_EQ(math.update(1.0F, 0.0F, 0), LV_DISPLAY_ROTATION_0);
}

TEST_CASE("a new rotation is only applied after enough consecutive stable readings") {
    OrientationMath math;
    math.reset(LV_DISPLAY_ROTATION_0);

    // The 1st call only sets the debounce target; 5 more consecutive agreeing calls are then needed.
    lv_display_rotation_t last = LV_DISPLAY_ROTATION_0;
    for (int i = 0; i < 5; i++) {
        last = math.update(1.0F, 0.0F, 0);
        CHECK_EQ(last, LV_DISPLAY_ROTATION_0); // not yet debounced
    }
    last = math.update(1.0F, 0.0F, 0);
    CHECK_EQ(last, LV_DISPLAY_ROTATION_90); // 6th call (5th consecutive stable reading) switches
}

TEST_CASE("all four gravity directions map to the expected rotation") {
    OrientationMath math;
    math.reset(LV_DISPLAY_ROTATION_0);

    auto settle = [&](float gx, float gy) {
        lv_display_rotation_t result = LV_DISPLAY_ROTATION_0;
        for (int i = 0; i < 6; i++) {
            result = math.update(gx, gy, 0);
        }
        return result;
    };

    CHECK_EQ(settle(0.0F, 1.0F), LV_DISPLAY_ROTATION_0);
    CHECK_EQ(settle(1.0F, 0.0F), LV_DISPLAY_ROTATION_90);
    CHECK_EQ(settle(0.0F, -1.0F), LV_DISPLAY_ROTATION_180);
    CHECK_EQ(settle(-1.0F, 0.0F), LV_DISPLAY_ROTATION_270);
}

TEST_CASE("mount rotation offsets which rotation a gravity direction maps to") {
    OrientationMath math;
    math.reset(LV_DISPLAY_ROTATION_0);

    lv_display_rotation_t result = LV_DISPLAY_ROTATION_0;
    for (int i = 0; i < 6; i++) {
        // Gravity toward +X normally means rotation 90, but a 1-quarter-turn mount offset shifts it by one more step.
        result = math.update(1.0F, 0.0F, 1);
    }
    CHECK_EQ(result, LV_DISPLAY_ROTATION_180);
}

TEST_CASE("an unstable target resets the debounce counter") {
    OrientationMath math;
    math.reset(LV_DISPLAY_ROTATION_0);

    // Get close to switching to rotation 90...
    for (int i = 0; i < 4; i++) {
        math.update(1.0F, 0.0F, 0);
    }
    // ...then a different target interrupts the debounce run.
    lv_display_rotation_t result = math.update(0.0F, -1.0F, 0);
    CHECK_EQ(result, LV_DISPLAY_ROTATION_0);

    // Confirms the rotation-180 debounce restarted from zero rather than continuing.
    for (int i = 0; i < 4; i++) {
        result = math.update(0.0F, -1.0F, 0);
        CHECK_EQ(result, LV_DISPLAY_ROTATION_0);
    }
    result = math.update(0.0F, -1.0F, 0);
    CHECK_EQ(result, LV_DISPLAY_ROTATION_180);
}
