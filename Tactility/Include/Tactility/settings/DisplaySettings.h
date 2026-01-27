#pragma once

#include <src/display/lv_display.h>

namespace tt::settings::display {

enum class Orientation {
    // In order of rotation (to make it easier to convert to LVGL rotation)
    Landscape,
    Portrait,
    LandscapeFlipped,
    PortraitFlipped,
};

enum class ScreensaverType {
    None,           // Just black screen
    BouncingBalls,
    Mystify,
    MatrixRain,
    Count           // Sentinel for bounds checking - must be last
};

struct DisplaySettings {
    Orientation orientation;
    uint8_t gammaCurve;
    uint8_t backlightDuty;
    bool backlightTimeoutEnabled;
    uint32_t backlightTimeoutMs; // 0 = Never
    ScreensaverType screensaverType = ScreensaverType::None;
};

/** Compares default settings with the function parameter to return the difference */
lv_display_rotation_t toLvglDisplayRotation(Orientation orientation);

bool load(DisplaySettings& settings);

DisplaySettings loadOrGetDefault();

DisplaySettings getDefault();

bool save(const DisplaySettings& settings);

} // namespace
