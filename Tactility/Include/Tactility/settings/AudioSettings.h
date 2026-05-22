#pragma once

#include <cstdint>

namespace tt::settings::audio {

struct AudioSettings {
    uint8_t volume = 70; // 0-100
    bool muted = false;
};

bool load(AudioSettings& settings);

AudioSettings loadOrGetDefault();

AudioSettings getDefault();

bool save(const AudioSettings& settings);

} // namespace
