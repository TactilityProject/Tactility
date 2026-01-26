#pragma once

#include <cstdint>

namespace tt::settings::keyboard {

struct KeyboardSettings {
    bool backlightEnabled = true;
    uint8_t backlightBrightness = 127; // 0-255
    bool backlightTimeoutEnabled = true;
    uint32_t backlightTimeoutMs = 60000; // Timeout in milliseconds
};

bool load(KeyboardSettings& settings);

KeyboardSettings loadOrGetDefault();

KeyboardSettings getDefault();

bool save(const KeyboardSettings& settings);

}
