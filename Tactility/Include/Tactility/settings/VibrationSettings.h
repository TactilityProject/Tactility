// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

namespace tt::settings::vibration {

struct VibrationSettings {
    /** Whether vibration feedback is allowed at all. */
    bool enabled;
    /** Strength percentage used for pulses, 0..100. */
    uint8_t strength;
};

bool load(VibrationSettings& settings);

VibrationSettings loadOrGetDefault();

VibrationSettings getDefault();

bool save(const VibrationSettings& settings);

} // namespace tt::settings::vibration
