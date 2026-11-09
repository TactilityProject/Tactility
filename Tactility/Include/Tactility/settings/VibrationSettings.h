// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

namespace tt::settings::vibration {

struct VibrationSettings {
    /** Whether vibration feedback is allowed at all. */
    bool enabled;
    /** Strength percentage used for pulses, 0..100. */
    uint8_t strength;
    /** Pulse duration in milliseconds. */
    uint32_t duration_ms;
};

/** Bounds, step and default for VibrationSettings::duration_ms. */
constexpr uint32_t DURATION_MIN_MS = 50;
constexpr uint32_t DURATION_MAX_MS = 1000;
constexpr uint32_t DURATION_STEP_MS = 50;
constexpr uint32_t DURATION_DEFAULT_MS = 250;

bool load(VibrationSettings& settings);

VibrationSettings loadOrGetDefault();

VibrationSettings getDefault();

bool save(const VibrationSettings& settings);

} // namespace tt::settings::vibration
