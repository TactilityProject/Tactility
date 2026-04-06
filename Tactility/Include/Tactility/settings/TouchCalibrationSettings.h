#pragma once

#include <cstdint>

namespace tt::settings::touch {

struct TouchCalibrationSettings {
    bool enabled = false;
    int32_t xMin = 0;
    int32_t xMax = 0;
    int32_t yMin = 0;
    int32_t yMax = 0;
};

TouchCalibrationSettings getDefault();

bool load(TouchCalibrationSettings& settings);

TouchCalibrationSettings loadOrGetDefault();

bool save(const TouchCalibrationSettings& settings);

bool isValid(const TouchCalibrationSettings& settings);

TouchCalibrationSettings getActive();

void setRuntimeCalibrationEnabled(bool enabled);

void invalidateCache();

bool applyCalibration(const TouchCalibrationSettings& settings, uint16_t xMax, uint16_t yMax, uint16_t& x, uint16_t& y);

} // namespace tt::settings::touch
