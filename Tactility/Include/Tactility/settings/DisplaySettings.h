#pragma once

#include <lvgl.h>

#include <tactility/device.h>

#include <stdint.h>

namespace tt::settings::display {

enum class Orientation {
    // In order of rotation (to make it easier to convert to LVGL rotation)
    Landscape,
    Portrait,
    LandscapeFlipped,
    PortraitFlipped,
};

struct DisplaySettings {
    Orientation orientation;
    uint8_t gammaCurve;
    uint8_t backlightDuty;
    bool autoRotateEnabled = false;
};

/** Compares default settings with the function parameter to return the difference */
lv_display_rotation_t toLvglDisplayRotation(Orientation orientation);

/** Inverse of toLvglDisplayRotation(): maps an LVGL rotation back to the Orientation that produces it. */
Orientation fromLvglDisplayRotation(lv_display_rotation_t rotation);

bool load(DisplaySettings& settings);

DisplaySettings loadOrGetDefault();

DisplaySettings getDefault();

bool save(const DisplaySettings& settings);

/** Changes only the orientation of the saved settings. Safe to call while other tasks save. */
bool saveOrientation(Orientation orientation);

/** Changes only the orientation and auto-rotate setting. Safe to call while other tasks save. */
bool saveOrientationAndAutoRotate(Orientation orientation, bool autoRotateEnabled);

/** Changes only the backlight duty of the saved settings. Safe to call while other tasks save. */
bool saveBacklightDuty(uint8_t duty);

/** Queues a coalesced asynchronous save of the latest backlight duty. */
bool queueSaveBacklightDuty(uint8_t duty);

/** The step of a brightness slider, in percent */
constexpr int32_t BACKLIGHT_STEP_PERCENT = 10;

/** The lowest brightness that can be chosen, in percent, so the display stays readable */
constexpr int32_t BACKLIGHT_MIN_PERCENT = 10;

/** @return the backlight of the first display, or nullptr when there is none. Release it with device_put(). */
Device* getBacklight();

/** @return true when the backlight has more than 2 levels, so a brightness can be chosen */
bool backlightSupportsBrightness(Device* backlight);

/** Converts a percentage of the backlight's brightness range to a brightness. Percentages below BACKLIGHT_MIN_PERCENT are raised to it. */
int32_t backlightPercentToBrightness(Device* backlight, int32_t percent);

/** The inverse of backlightPercentToBrightness(), which gives the percentage of a step for backlights with few levels */
int32_t backlightBrightnessToPercent(Device* backlight, int32_t brightness);

} // namespace
