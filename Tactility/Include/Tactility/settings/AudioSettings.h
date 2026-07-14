#pragma once

namespace tt::settings::audio {

struct AudioSettings {
    bool inputEnabled;
    bool outputEnabled;
    bool inputMuted;
    bool outputMuted;
    float inputVolume;  // 0..100
    float outputVolume; // 0..100
};

bool load(AudioSettings& settings);

AudioSettings loadOrGetDefault();

AudioSettings getDefault();

bool save(const AudioSettings& settings);

} // namespace tt::settings::audio
