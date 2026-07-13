#pragma once

#include <cstdint>

namespace tt::settings::touch {

/**
 * @brief Persisted touch calibration coefficients.
 *
 * Shape mirrors struct LvglPointerCalibration (tactility/lvgl_pointer.h): xMin/xMax/yMin/yMax are
 * the raw touch coordinate range that should map onto the display's full resolution. This struct
 * only concerns itself with persistence - applying it to a live pointer indev is the caller's
 * responsibility (see lvgl_pointer_set_calibration()).
 */
struct TouchCalibrationSettings {
    bool enabled = false;
    int32_t xMin = 0;
    int32_t xMax = 0;
    int32_t yMin = 0;
    int32_t yMax = 0;
};

TouchCalibrationSettings getDefault();

bool isValid(const TouchCalibrationSettings& settings);

bool load(TouchCalibrationSettings& settings);

TouchCalibrationSettings loadOrGetDefault();

bool save(const TouchCalibrationSettings& settings);

} // namespace tt::settings::touch
