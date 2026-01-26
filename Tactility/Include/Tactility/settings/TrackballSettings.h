#pragma once

#include <cstdint>

namespace tt::settings::trackball {

enum class TrackballMode : uint8_t {
    Encoder = 0,  // Scroll wheel navigation (default)
    Pointer = 1   // Mouse cursor mode
};

struct TrackballSettings {
    bool trackballEnabled = false;
    TrackballMode trackballMode = TrackballMode::Encoder;
    uint8_t encoderSensitivity = 1;   // Steps per tick (1-10)
    uint8_t pointerSensitivity = 10;  // Pixels per tick (1-10)
};

bool load(TrackballSettings& settings);

TrackballSettings loadOrGetDefault();

TrackballSettings getDefault();

bool save(const TrackballSettings& settings);

}
